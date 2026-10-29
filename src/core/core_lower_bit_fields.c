#include "core/core_lower_internal.h"

#include "frontend/expression_semantics.h"
#include <stdint.h>
#include <string.h>

/* Bit-fields are value-level C semantics, not target-machine instructions.
 * Keep reads, compound RMW and prefix/postfix update together so that width,
 * signedness, storage-unit access, volatile semantics and one-time source
 * evaluation stay consistent. The target DataLayout is still context-owned.
 * No new Core IR opcode, calling convention, or machine ABI is introduced. */

/* All bit-field reads, assignments, compound RMW and increments share one
 * C semantic owner. Snapshot source before evaluating target base, as before. */

/* A declared `unsigned int` bit-field is NOT necessarily backed by four
 * addressable bytes. In a packed record, DataLayout can place a ten-bit field
 * at byte three of a five-byte object; loading uint32_t there would read past
 * the object (and often cross a page boundary). A nonnaturally aligned field
 * can also fault on machines that reject unaligned scalar loads. Keep the
 * machine-efficient full-width access for safe aligned windows, otherwise
 * access exactly the bytes covering this field's bits. */
static bool core_bit_field_access_window(
    const MinicCoreLowerContext *context, const MinicRecord *record,
    MinicRecordId record_id, size_t byte_offset, size_t bit_offset,
    size_t field_width, unsigned int *storage_width,
    MinicType *storage_type, MinicType *access_type,
    size_t *byte_count, bool *narrow) {
    size_t record_size;
    size_t record_alignment;
    size_t storage_bytes;

    if (context == NULL || context->body == NULL || record == NULL ||
        storage_type == NULL || storage_width == NULL ||
        access_type == NULL || byte_count == NULL || narrow == NULL ||
        field_width == 0U || *storage_width == 0U ||
        *storage_width > 64U || (*storage_width % 8U) != 0U ||
        bit_offset >= 8U || field_width > 64U - bit_offset ||
        !minic_data_layout_is_little_endian(core_data_layout(context)) ||
        !minic_data_layout_type(core_data_layout(context),
                                context->body->program,
                                minic_type_record(record_id),
                                &record_size, &record_alignment)) {
        return false;
    }
    /* A 32-bit field may start at bit 1 of a packed record. Its five physical
     * bytes require a 64-bit *transport* integer even though the C value and
     * its promotions remain 32-bit. Keep the original semantic type separate
     * in each caller; only storage extraction/merge uses this wider type. */
    if (field_width > (size_t)(*storage_width - bit_offset)) {
        unsigned int wide_width;
        MinicType wide_type = minic_type_unsigned_long_long();
        if (!minic_target_info_integer_width(
                context->target, context->body->program,
                wide_type, &wide_width) || wide_width != 64U)
            return false;
        *storage_type = wide_type;
        *storage_width = wide_width;
    }
    storage_bytes = *storage_width / 8U;
    *byte_count = (bit_offset + field_width + 7U) / 8U;
    if (*byte_count == 0U || *byte_count > storage_bytes ||
        byte_offset > record_size || *byte_count > record_size - byte_offset) {
        return false;
    }
    *narrow = record_alignment < storage_bytes ||
              (byte_offset % storage_bytes) != 0U ||
              storage_bytes > record_size - byte_offset;
    *access_type = *narrow ? minic_type_unsigned_char() : *storage_type;
    return true;
}

static MinicCoreLowerStatus core_bit_field_byte_address(
    MinicCoreLowerContext *context, MinicSourceSpan span,
    MinicCoreValueId base, size_t byte_index, MinicCoreValueId *address) {
    MinicCoreInstruction op;
    MinicCoreValueId index;

    if (byte_index == 0U) {
        *address = base;
        return MINIC_CORE_LOWER_OK;
    }
    (void)memset(&op, 0, sizeof(op));
    op.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
    op.span = span;
    op.type = minic_type_unsigned_int();
    op.result = MINIC_CORE_VALUE_INVALID;
    op.value.integer_value = (int64_t)byte_index;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &op, &index))
        return MINIC_CORE_LOWER_ERROR;
    (void)memset(&op, 0, sizeof(op));
    op.kind = MINIC_CORE_INSTRUCTION_POINTER_OFFSET;
    op.span = span;
    op.type = context->function->values[base].type;
    op.result = MINIC_CORE_VALUE_INVALID;
    op.value.pointer_offset.base = base;
    op.value.pointer_offset.index = index;
    op.value.pointer_offset.element_size = 1U;
    return minic_core_function_append_value_instruction(
               context->function, context->block_id, &op, address)
               ? MINIC_CORE_LOWER_OK : MINIC_CORE_LOWER_ERROR;
}

static MinicCoreLowerStatus core_bit_field_load_storage(
    MinicCoreLowerContext *context, MinicSourceSpan span,
    MinicCoreValueId address, MinicType storage_type,
    bool narrow, size_t byte_count, bool is_volatile,
    MinicCoreValueId *value) {
    MinicCoreInstruction op;
    MinicCoreValueId assembled = MINIC_CORE_VALUE_INVALID;
    size_t byte_index;

    if (!narrow) {
        (void)memset(&op, 0, sizeof(op));
        op.kind = MINIC_CORE_INSTRUCTION_LOAD;
        op.span = span;
        op.type = storage_type;
        op.result = MINIC_CORE_VALUE_INVALID;
        op.value.load.address = address;
        op.value.load.is_volatile = is_volatile;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &op, value)
                   ? MINIC_CORE_LOWER_OK : MINIC_CORE_LOWER_ERROR;
    }
    /* The access-window gate requires explicit little-endian DataLayout;
     * no Core architecture switch or assumption about a future BE ISA. */
    for (byte_index = 0U; byte_index < byte_count; ++byte_index) {
        MinicCoreValueId byte_address;
        MinicCoreValueId current;
        MinicCoreValueId shift;
        MinicCoreValueId shifted;
        MinicCoreLowerStatus status = core_bit_field_byte_address(
            context, span, address, byte_index, &byte_address);
        if (status != MINIC_CORE_LOWER_OK) return status;
        (void)memset(&op, 0, sizeof(op));
        op.kind = MINIC_CORE_INSTRUCTION_LOAD;
        op.span = span;
        op.type = minic_type_unsigned_char();
        op.result = MINIC_CORE_VALUE_INVALID;
        op.value.load.address = byte_address;
        op.value.load.is_volatile = is_volatile;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &op, &current))
            return MINIC_CORE_LOWER_ERROR;
        status = minic_core_lower_internal_append_integer_conversion(
            context, span, storage_type, current, &current);
        if (status != MINIC_CORE_LOWER_OK) return status;
        if (byte_index != 0U) {
            (void)memset(&op, 0, sizeof(op));
            op.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
            op.span = span;
            op.type = minic_type_unsigned_int();
            op.result = MINIC_CORE_VALUE_INVALID;
            op.value.integer_value = (int64_t)(byte_index * 8U);
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &op, &shift))
                return MINIC_CORE_LOWER_ERROR;
            (void)memset(&op, 0, sizeof(op));
            op.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT;
            op.span = span;
            op.type = storage_type;
            op.result = MINIC_CORE_VALUE_INVALID;
            op.value.binary.left = current;
            op.value.binary.right = shift;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &op, &shifted))
                return MINIC_CORE_LOWER_ERROR;
            (void)memset(&op, 0, sizeof(op));
            op.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR;
            op.span = span;
            op.type = storage_type;
            op.result = MINIC_CORE_VALUE_INVALID;
            op.value.binary.left = assembled;
            op.value.binary.right = shifted;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &op, &assembled))
                return MINIC_CORE_LOWER_ERROR;
        } else {
            assembled = current;
        }
    }
    *value = assembled;
    return MINIC_CORE_LOWER_OK;
}

static MinicCoreLowerStatus core_bit_field_store_storage(
    MinicCoreLowerContext *context, MinicSourceSpan span,
    MinicCoreValueId address, MinicCoreValueId value,
    bool narrow, size_t byte_count, bool is_volatile) {
    MinicCoreInstruction op;
    size_t byte_index;

    if (!narrow) {
        (void)memset(&op, 0, sizeof(op));
        op.kind = MINIC_CORE_INSTRUCTION_STORE;
        op.span = span;
        op.type = minic_type_void();
        op.result = MINIC_CORE_VALUE_INVALID;
        op.value.store.address = address;
        op.value.store.stored_value = value;
        op.value.store.is_volatile = is_volatile;
        return minic_core_function_append_effect_instruction(
                   context->function, context->block_id, &op)
                   ? MINIC_CORE_LOWER_OK : MINIC_CORE_LOWER_ERROR;
    }
    for (byte_index = 0U; byte_index < byte_count; ++byte_index) {
        MinicCoreValueId byte_address;
        MinicCoreValueId byte_value = value;
        MinicCoreLowerStatus status = core_bit_field_byte_address(
            context, span, address, byte_index, &byte_address);
        if (status != MINIC_CORE_LOWER_OK) return status;
        if (byte_index != 0U) {
            MinicCoreValueId shift;
            (void)memset(&op, 0, sizeof(op));
            op.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
            op.span = span;
            op.type = minic_type_unsigned_int();
            op.result = MINIC_CORE_VALUE_INVALID;
            op.value.integer_value = (int64_t)(byte_index * 8U);
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &op, &shift))
                return MINIC_CORE_LOWER_ERROR;
            (void)memset(&op, 0, sizeof(op));
            op.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT;
            op.span = span;
            op.type = context->function->values[value].type;
            op.result = MINIC_CORE_VALUE_INVALID;
            op.value.binary.left = value;
            op.value.binary.right = shift;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &op, &byte_value))
                return MINIC_CORE_LOWER_ERROR;
        }
        status = minic_core_lower_internal_append_integer_conversion(
            context, span, minic_type_unsigned_char(), byte_value, &byte_value);
        if (status != MINIC_CORE_LOWER_OK) return status;
        (void)memset(&op, 0, sizeof(op));
        op.kind = MINIC_CORE_INSTRUCTION_STORE;
        op.span = span;
        op.type = minic_type_void();
        op.result = MINIC_CORE_VALUE_INVALID;
        op.value.store.address = byte_address;
        op.value.store.stored_value = byte_value;
        op.value.store.is_volatile = is_volatile;
        if (!minic_core_function_append_effect_instruction(
                context->function, context->block_id, &op))
            return MINIC_CORE_LOWER_ERROR;
    }
    return MINIC_CORE_LOWER_OK;
}

MinicCoreLowerStatus minic_core_lower_try_bit_field_assignment(
    MinicCoreLowerContext *context, const MinicExpression *target,
    MinicExpressionId source_id, MinicSourceSpan span,
    MinicCoreValueId *result_value, bool is_initializer_write, bool *matched) {
    if (context == NULL || context->body == NULL ||
        context->body->program == NULL || context->function == NULL ||
        target == NULL || matched == NULL) return MINIC_CORE_LOWER_ERROR;
    *matched = false;
    /* BATCH_P_UNSIGNED_BIT_FIELD_WRITE: bit-fields are not C-addressable, so
       lower a simple unsigned bit-field assignment as one storage-unit RMW.
       Reuse the same field-layout/address seam as the established unsigned
       bit-field read. Signed bit-field writes remain fail-closed. */
    if (target->kind == MINIC_EXPRESSION_MEMBER) {
        const MinicExpression *base;
        const MinicRecord *record;
        const MinicRecordField *field;

        base = minic_c0_program_expression(context->body->program, target->value.member.base);
        record = minic_c0_program_record(context->body->program, target->value.member.record_id);
        field = minic_c0_record_field(record, target->value.member.field_index);
        if (field != NULL && field->is_bit_field) {
            *matched = true;
            MinicCoreInstruction operation;
            MinicCoreObjectId source_object;
            MinicCoreValueId address;
            MinicCoreValueId base_value;
            MinicCoreValueId current;
            MinicCoreValueId field_value;
            MinicCoreValueId field_storage;
            MinicCoreValueId assigned_value;
            MinicCoreValueId constant;
            MinicCoreValueId merged;
            MinicCoreLowerStatus bit_status;
            MinicType base_value_type;
            MinicType record_type;
            MinicType storage_access_type;
            MinicType storage_type;
            MinicType value_type;
            size_t byte_offset;
            size_t bit_offset;
            size_t byte_count;
            unsigned int storage_width;
            unsigned int semantic_width;
            bool narrow;
            uint64_t low_mask;
            uint64_t field_mask;
            uint64_t clear_mask;
            uint64_t storage_mask;

            if (base == NULL || record == NULL || field->bit_width == 0U ||
                !minic_type_unqualified(target->type, &value_type) ||
                !minic_type_is_integer(value_type) ||
                (!core_unsigned_bit_field_semantic_type(context, value_type) &&
                 !minic_type_is_signed_integer(value_type)) ||
                /* Const qualification prohibits mutation, not initialization.
                 * Only the parser's aggregate-initializer writes carry the
                 * provenance needed to bypass this semantic guard. */
                (minic_type_is_const(target->type) && !is_initializer_write) ||
                !core_bit_field_storage_type(
                    context, value_type, &storage_type, &storage_width) ||
                storage_width == 0U || storage_width > 64U ||
                field->bit_width > storage_width ||
                !minic_data_layout_record_field_layout(core_data_layout(context),
                                                       context->body->program,
                                                       record,
                                                       target->value.member.field_index,
                                                       &byte_offset,
                                                       &bit_offset) ||
                !core_scalar_expression_value_type(context->body, base, &base_value_type) ||
                !minic_type_is_pointer(base_value_type) ||
                !minic_type_pointee(base_value_type, &record_type) ||
                !minic_type_is_record(record_type) ||
                record_type.record_id != target->value.member.record_id) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            semantic_width = storage_width;
            if (!core_bit_field_access_window(
                    context, record, target->value.member.record_id,
                    byte_offset, bit_offset, field->bit_width, &storage_width,
                    &storage_type, &storage_access_type, &byte_count, &narrow))
                return MINIC_CORE_LOWER_UNSUPPORTED;
            if (minic_type_is_volatile(target->type) &&
                !minic_type_add_volatile(storage_access_type, &storage_access_type)) {
                return MINIC_CORE_LOWER_ERROR;
            }

            bit_status = minic_core_lower_internal_lower_scalar_assignment_value(
                context, value_type, source_id, &field_value);
            if (bit_status != MINIC_CORE_LOWER_OK) {
                return bit_status;
            }
            bit_status = minic_core_lower_internal_spill_scalar_value(
                context, span, value_type, field_value, &source_object);
            if (bit_status != MINIC_CORE_LOWER_OK) {
                return bit_status;
            }

            bit_status = lower_expression(context, target->value.member.base, &base_value);
            if (bit_status != MINIC_CORE_LOWER_OK) {
                return bit_status;
            }
            if (base_value >= context->function->value_count ||
                !minic_type_equal(context->function->values[base_value].type, base_value_type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            bit_status = minic_core_lower_internal_append_field_address(context,
                                              target->span,
                                              base_value,
                                              target->value.member.record_id,
                                              target->value.member.field_index,
                                              storage_access_type,
                                              &address);
            if (bit_status != MINIC_CORE_LOWER_OK) {
                return bit_status;
            }
            bit_status = core_bit_field_load_storage(
                context, target->span, address, storage_type, narrow,
                byte_count, minic_type_is_volatile(target->type), &current);
            if (bit_status != MINIC_CORE_LOWER_OK) return bit_status;
            bit_status = minic_core_lower_internal_reload_scalar_value(
                context, span, value_type, source_object, &field_value);
            if (bit_status != MINIC_CORE_LOWER_OK) {
                return bit_status;
            }
            if (minic_type_equal(storage_type, value_type)) {
                field_storage = field_value;
            } else {
                bit_status = minic_core_lower_internal_append_integer_conversion(
                    context, span, storage_type, field_value, &field_storage);
                if (bit_status != MINIC_CORE_LOWER_OK) {
                    return bit_status;
                }
            }

            low_mask = field->bit_width == 64U
                           ? UINT64_MAX
                           : ((UINT64_C(1) << field->bit_width) - UINT64_C(1));
            if (field->bit_width < storage_width) {
                (void)memset(&operation, 0, sizeof(operation));
                operation.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
                operation.span = span;
                operation.type = storage_type;
                operation.result = MINIC_CORE_VALUE_INVALID;
                (void)memcpy(&operation.value.integer_value, &low_mask, sizeof(low_mask));
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &operation, &constant)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                (void)memset(&operation, 0, sizeof(operation));
                operation.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND;
                operation.span = span;
                operation.type = storage_type;
                operation.result = MINIC_CORE_VALUE_INVALID;
                operation.value.binary.left = field_storage;
                operation.value.binary.right = constant;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &operation, &field_storage)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
            }
            if (minic_type_equal(storage_type, value_type)) {
                assigned_value = field_storage;
            } else {
                bit_status = minic_core_lower_internal_append_integer_conversion(
                    context, span, value_type, field_storage, &assigned_value);
                if (bit_status != MINIC_CORE_LOWER_OK) {
                    return bit_status;
                }
            }
            /* M153_SIGNED_BIT_FIELD_WRITE_OWNER: storage is merged through an
               unsigned allocation unit.  For a signed field, reconstruct the
               assignment-expression value from the truncated field bits using
               the same shift-left/arithmetic-shift-right sign extension as the
               established M103 read path.  The stored bits themselves remain
               the masked two's-complement representation. */
            if (minic_type_is_signed_integer(value_type) &&
                field->bit_width < semantic_width) {
                MinicCoreValueId shift;
                uint64_t shift_bits = (uint64_t)(semantic_width - field->bit_width);

                (void)memset(&operation, 0, sizeof(operation));
                operation.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
                operation.span = span;
                operation.type = minic_type_unsigned_int();
                operation.result = MINIC_CORE_VALUE_INVALID;
                (void)memcpy(&operation.value.integer_value, &shift_bits, sizeof(shift_bits));
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &operation, &shift)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                (void)memset(&operation, 0, sizeof(operation));
                operation.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT;
                operation.span = span;
                operation.type = value_type;
                operation.result = MINIC_CORE_VALUE_INVALID;
                operation.value.binary.left = assigned_value;
                operation.value.binary.right = shift;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &operation, &assigned_value)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                (void)memset(&operation, 0, sizeof(operation));
                operation.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT;
                operation.span = span;
                operation.type = value_type;
                operation.result = MINIC_CORE_VALUE_INVALID;
                operation.value.binary.left = assigned_value;
                operation.value.binary.right = shift;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &operation, &assigned_value)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
            }
            if (bit_offset != 0U) {
                uint64_t shift_bits = (uint64_t)bit_offset;

                (void)memset(&operation, 0, sizeof(operation));
                operation.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
                operation.span = span;
                operation.type = storage_type;
                operation.result = MINIC_CORE_VALUE_INVALID;
                (void)memcpy(&operation.value.integer_value, &shift_bits, sizeof(shift_bits));
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &operation, &constant)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                (void)memset(&operation, 0, sizeof(operation));
                operation.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT;
                operation.span = span;
                operation.type = storage_type;
                operation.result = MINIC_CORE_VALUE_INVALID;
                operation.value.binary.left = field_storage;
                operation.value.binary.right = constant;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &operation, &field_storage)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
            }

            field_mask = low_mask << bit_offset;
            storage_mask = storage_width == 64U
                               ? UINT64_MAX
                               : ((UINT64_C(1) << storage_width) - UINT64_C(1));
            clear_mask = (~field_mask) & storage_mask;
            (void)memset(&operation, 0, sizeof(operation));
            operation.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
            operation.span = span;
            operation.type = storage_type;
            operation.result = MINIC_CORE_VALUE_INVALID;
            (void)memcpy(&operation.value.integer_value, &clear_mask, sizeof(clear_mask));
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &operation, &constant)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&operation, 0, sizeof(operation));
            operation.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND;
            operation.span = span;
            operation.type = storage_type;
            operation.result = MINIC_CORE_VALUE_INVALID;
            operation.value.binary.left = current;
            operation.value.binary.right = constant;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &operation, &merged)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&operation, 0, sizeof(operation));
            operation.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR;
            operation.span = span;
            operation.type = storage_type;
            operation.result = MINIC_CORE_VALUE_INVALID;
            operation.value.binary.left = merged;
            operation.value.binary.right = field_storage;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &operation, &merged)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            bit_status = core_bit_field_store_storage(
                context, span, address, merged, narrow, byte_count,
                minic_type_is_volatile(target->type));
            if (bit_status != MINIC_CORE_LOWER_OK) return bit_status;
            if (result_value != NULL) {
                *result_value = assigned_value;
            }
            return MINIC_CORE_LOWER_OK;
        }
    }
    return MINIC_CORE_LOWER_OK;
}

MinicCoreLowerStatus minic_core_lower_try_bit_field_read(
    MinicCoreLowerContext *context, const MinicExpression *expression,
    MinicCoreValueId *value_id, bool *matched) {
    *matched = false;
    /* M103_INTEGER_BIT_FIELD_READ: a bit-field is not C-addressable, but
       reading it is a scalar operation. Form the storage-unit address
       internally, load it through an unsigned storage type, extract the field,
       then explicitly sign-extend signed fields from their declared bit width.
       Bit-field writes remain owned by their dedicated RMW lowering paths. */
    if (expression->kind == MINIC_EXPRESSION_MEMBER &&
        expression->value_category == MINIC_VALUE_LVALUE) {
        const MinicExpression *base;
        const MinicRecord *record;
        const MinicRecordField *field;
        MinicCoreInstruction extract;
        MinicCoreValueId address_id;
        MinicCoreValueId base_id;
        MinicCoreValueId current;
        MinicCoreValueId rhs;
        MinicCoreLowerStatus status;
        MinicType base_value_type;
        MinicType record_type;
        MinicType storage_access_type;
        MinicType storage_type;
        MinicType value_type;
        size_t byte_offset;
        size_t bit_offset;
        size_t byte_count;
        unsigned int storage_width;
        bool narrow;
        uint64_t mask_bits;

        base = minic_c0_program_expression(context->body->program, expression->value.member.base);
        record = minic_c0_program_record(context->body->program, expression->value.member.record_id);
        field = minic_c0_record_field(record, expression->value.member.field_index);
        if (field != NULL && field->is_bit_field) {
            *matched = true;
            if (base == NULL || record == NULL || field->bit_width == 0U ||
                !minic_type_unqualified(expression->type, &value_type) ||
                !minic_type_is_integer(value_type) ||
                !core_bit_field_storage_type(
                    context, value_type, &storage_type, &storage_width) ||
                storage_width == 0U || storage_width > 64U ||
                field->bit_width > storage_width ||
                !minic_data_layout_record_field_layout(core_data_layout(context),
                                                       context->body->program,
                                                       record,
                                                       expression->value.member.field_index,
                                                       &byte_offset,
                                                       &bit_offset) ||
                !core_scalar_expression_value_type(context->body, base, &base_value_type) ||
                !minic_type_is_pointer(base_value_type) ||
                !minic_type_pointee(base_value_type, &record_type) ||
                !minic_type_is_record(record_type) ||
                record_type.record_id != expression->value.member.record_id) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            if (!core_bit_field_access_window(
                    context, record, expression->value.member.record_id,
                    byte_offset, bit_offset, field->bit_width, &storage_width,
                    &storage_type, &storage_access_type, &byte_count, &narrow))
                return MINIC_CORE_LOWER_UNSUPPORTED;
            if (minic_type_is_const(expression->type) &&
                !minic_type_add_const(storage_access_type, &storage_access_type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (minic_type_is_volatile(expression->type) &&
                !minic_type_add_volatile(storage_access_type, &storage_access_type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            status = lower_expression(context, expression->value.member.base, &base_id);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            if (base_id >= context->function->value_count ||
                !minic_type_equal(context->function->values[base_id].type, base_value_type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            status = minic_core_lower_internal_append_field_address(context,
                                          expression->span,
                                          base_id,
                                          expression->value.member.record_id,
                                          expression->value.member.field_index,
                                          storage_access_type,
                                          &address_id);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            status = core_bit_field_load_storage(
                context, expression->span, address_id, storage_type, narrow,
                byte_count, minic_type_is_volatile(expression->type), &current);
            if (status != MINIC_CORE_LOWER_OK) return status;
            if (bit_offset != 0U) {
                (void)memset(&extract, 0, sizeof(extract));
                extract.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
                extract.span = expression->span;
                extract.type = minic_type_unsigned_int();
                extract.result = MINIC_CORE_VALUE_INVALID;
                extract.value.integer_value = (int64_t)bit_offset;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &extract, &rhs)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                (void)memset(&extract, 0, sizeof(extract));
                extract.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT;
                extract.span = expression->span;
                extract.type = storage_type;
                extract.result = MINIC_CORE_VALUE_INVALID;
                extract.value.binary.left = current;
                extract.value.binary.right = rhs;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &extract, &current)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
            }
            if (field->bit_width < storage_width) {
                mask_bits = field->bit_width == 64U
                                ? UINT64_MAX
                                : ((UINT64_C(1) << field->bit_width) - UINT64_C(1));
                (void)memset(&extract, 0, sizeof(extract));
                extract.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
                extract.span = expression->span;
                extract.type = storage_type;
                extract.result = MINIC_CORE_VALUE_INVALID;
                (void)memcpy(&extract.value.integer_value, &mask_bits, sizeof(mask_bits));
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &extract, &rhs)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                (void)memset(&extract, 0, sizeof(extract));
                extract.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND;
                extract.span = expression->span;
                extract.type = storage_type;
                extract.result = MINIC_CORE_VALUE_INVALID;
                extract.value.binary.left = current;
                extract.value.binary.right = rhs;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &extract, &current)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
            }
            if (minic_type_is_unsigned_integer(value_type)) {
                if (minic_type_equal(storage_type, value_type)) {
                    *value_id = current;
                    return MINIC_CORE_LOWER_OK;
                }
                return minic_core_lower_internal_append_integer_conversion(
                    context, expression->span, value_type, current, value_id);
            }
            {
                MinicCoreValueId signed_current;
                unsigned int value_width;

                if (!minic_target_info_integer_width(
                        context->target, context->body->program, value_type, &value_width) ||
                    field->bit_width > value_width) {
                    return MINIC_CORE_LOWER_UNSUPPORTED;
                }
                status = minic_core_lower_internal_append_integer_conversion(
                    context, expression->span, value_type, current, &signed_current);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
                if (field->bit_width < value_width) {
                    MinicCoreValueId shift;
                    uint64_t shift_bits = (uint64_t)(value_width - field->bit_width);

                    (void)memset(&extract, 0, sizeof(extract));
                    extract.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
                    extract.span = expression->span;
                    extract.type = minic_type_unsigned_int();
                    extract.result = MINIC_CORE_VALUE_INVALID;
                    (void)memcpy(&extract.value.integer_value, &shift_bits, sizeof(shift_bits));
                    if (!minic_core_function_append_value_instruction(
                            context->function, context->block_id, &extract, &shift)) {
                        return MINIC_CORE_LOWER_ERROR;
                    }
                    (void)memset(&extract, 0, sizeof(extract));
                    extract.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT;
                    extract.span = expression->span;
                    extract.type = value_type;
                    extract.result = MINIC_CORE_VALUE_INVALID;
                    extract.value.binary.left = signed_current;
                    extract.value.binary.right = shift;
                    if (!minic_core_function_append_value_instruction(
                            context->function, context->block_id, &extract, &signed_current)) {
                        return MINIC_CORE_LOWER_ERROR;
                    }
                    (void)memset(&extract, 0, sizeof(extract));
                    extract.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT;
                    extract.span = expression->span;
                    extract.type = value_type;
                    extract.result = MINIC_CORE_VALUE_INVALID;
                    extract.value.binary.left = signed_current;
                    extract.value.binary.right = shift;
                    if (!minic_core_function_append_value_instruction(
                            context->function, context->block_id, &extract, &signed_current)) {
                        return MINIC_CORE_LOWER_ERROR;
                    }
                }
                *value_id = signed_current;
                return MINIC_CORE_LOWER_OK;
            }
        }
    }
    return MINIC_CORE_LOWER_OK;
}

MinicCoreLowerStatus minic_core_lower_try_bit_field_compound(
    MinicCoreLowerContext *context, const MinicExpression *expression,
    MinicCoreValueId *value_id, bool *matched) {
    *matched = false;
    /* M101_UNSIGNED_BIT_FIELD_COMPOUND_ASSIGNMENT: a bit-field is not a
       C-addressable lvalue, but compound assignment still evaluates its base
       once, reads the field value, performs the promoted operation, then
       writes the converted result back through one storage-unit RMW. */
    if (expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT) {
        const MinicExpression *bit_target;
        const MinicExpression *bit_source;
        const MinicExpression *bit_base;
        const MinicRecord *bit_record;
        const MinicRecordField *bit_field;

        bit_target = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        bit_source = minic_c0_program_expression(
            context->body->program, expression->value.binary.right);
        bit_base = bit_target != NULL && bit_target->kind == MINIC_EXPRESSION_MEMBER
                       ? minic_c0_program_expression(
                             context->body->program, bit_target->value.member.base)
                       : NULL;
        bit_record = bit_target != NULL && bit_target->kind == MINIC_EXPRESSION_MEMBER
                         ? minic_c0_program_record(
                               context->body->program, bit_target->value.member.record_id)
                         : NULL;
        bit_field = bit_target != NULL && bit_target->kind == MINIC_EXPRESSION_MEMBER
                        ? minic_c0_record_field(
                              bit_record, bit_target->value.member.field_index)
                        : NULL;
        if (bit_field != NULL && bit_field->is_bit_field) {
            *matched = true;
            MinicCoreInstruction bit_instruction;
            MinicCoreObjectId bit_address_object;
            MinicCoreObjectId bit_current_object;
            MinicCoreValueId bit_address;
            MinicCoreValueId bit_base_value;
            MinicCoreValueId bit_constant;
            MinicCoreValueId bit_current;
            MinicCoreValueId bit_current_common;
            MinicCoreValueId bit_field_storage;
            MinicCoreValueId bit_merge_current;
            MinicCoreValueId bit_merged;
            MinicCoreValueId bit_result;
            MinicCoreValueId bit_rhs;
            MinicCoreValueId bit_rhs_common;
            MinicCoreValueId bit_stored_value;
            MinicCoreLowerStatus bit_status;
            MinicType bit_address_type;
            MinicType bit_base_value_type;
            MinicType bit_common_type;
            MinicType bit_expression_value_type;
            MinicType bit_record_type;
            MinicType bit_right_type;
            MinicType bit_storage_access_type;
            MinicType bit_storage_type;
            MinicType bit_value_type;
            size_t bit_byte_offset;
            size_t bit_offset;
            size_t bit_byte_count;
            unsigned int bit_storage_width;
            unsigned int bit_semantic_width;
            uint64_t bit_clear_mask;
            uint64_t bit_field_mask;
            uint64_t bit_low_mask;
            uint64_t bit_storage_mask;
            bool bit_shift_assignment;
            bool bit_narrow;

            if (bit_target == NULL || bit_source == NULL || bit_base == NULL ||
                bit_record == NULL || bit_target->value_category != MINIC_VALUE_LVALUE ||
                bit_field->bit_width == 0U || minic_type_is_const(bit_target->type) ||
                !minic_type_unqualified(bit_target->type, &bit_value_type) ||
                !minic_type_is_integer(bit_value_type) ||
                (!core_unsigned_bit_field_semantic_type(context, bit_value_type) &&
                 !minic_type_is_signed_integer(bit_value_type)) ||
                !minic_type_is_integer(bit_source->type) || context->target == NULL ||
                !minic_type_unqualified(expression->type, &bit_expression_value_type) ||
                !minic_type_equal(bit_expression_value_type, bit_value_type) ||
                !core_bit_field_storage_type(
                    context, bit_value_type, &bit_storage_type, &bit_storage_width) ||
                bit_storage_width == 0U || bit_storage_width > 64U ||
                bit_field->bit_width > bit_storage_width ||
                !minic_data_layout_record_field_layout(core_data_layout(context),
                                                       context->body->program,
                                                       bit_record,
                                                       bit_target->value.member.field_index,
                                                       &bit_byte_offset,
                                                       &bit_offset) ||
                !core_scalar_expression_value_type(
                    context->body, bit_base, &bit_base_value_type) ||
                !minic_type_is_pointer(bit_base_value_type) ||
                !minic_type_pointee(bit_base_value_type, &bit_record_type) ||
                !minic_type_is_record(bit_record_type) ||
                bit_record_type.record_id != bit_target->value.member.record_id) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            bit_semantic_width = bit_storage_width;
            if (!core_bit_field_access_window(
                    context, bit_record, bit_target->value.member.record_id,
                    bit_byte_offset, bit_offset, bit_field->bit_width, &bit_storage_width,
                    &bit_storage_type, &bit_storage_access_type, &bit_byte_count,
                    &bit_narrow))
                return MINIC_CORE_LOWER_UNSUPPORTED;
            bit_shift_assignment =
                expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_LEFT ||
                expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_RIGHT;
            if (bit_shift_assignment) {
                if (!minic_target_info_integer_promotion_for_program(
                        context->target,
                        context->body->program,
                        bit_value_type,
                        &bit_common_type) ||
                    !minic_target_info_integer_promotion_for_program(
                        context->target,
                        context->body->program,
                        bit_source->type,
                        &bit_right_type)) {
                    return MINIC_CORE_LOWER_UNSUPPORTED;
                }
            } else {
                if (!minic_target_info_integer_common_for_program(context->target,
                                                                  context->body->program,
                                                                  bit_value_type,
                                                                  bit_source->type,
                                                                  &bit_common_type)) {
                    return MINIC_CORE_LOWER_UNSUPPORTED;
                }
                bit_right_type = bit_common_type;
            }
            if (minic_type_is_volatile(bit_target->type) &&
                !minic_type_add_volatile(bit_storage_access_type, &bit_storage_access_type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            bit_status = lower_expression(
                context, bit_target->value.member.base, &bit_base_value);
            if (bit_status != MINIC_CORE_LOWER_OK) {
                return bit_status;
            }
            if (bit_base_value >= context->function->value_count ||
                !minic_type_equal(context->function->values[bit_base_value].type,
                                  bit_base_value_type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            bit_status = minic_core_lower_internal_append_field_address(context,
                                              bit_target->span,
                                              bit_base_value,
                                              bit_target->value.member.record_id,
                                              bit_target->value.member.field_index,
                                              bit_storage_access_type,
                                              &bit_address);
            if (bit_status != MINIC_CORE_LOWER_OK) {
                return bit_status;
            }
            bit_status = core_bit_field_load_storage(
                context, bit_target->span, bit_address, bit_storage_type,
                bit_narrow, bit_byte_count,
                minic_type_is_volatile(bit_target->type), &bit_current);
            if (bit_status != MINIC_CORE_LOWER_OK) return bit_status;
            if (bit_offset != 0U) {
                (void)memset(&bit_instruction, 0, sizeof(bit_instruction));
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
                bit_instruction.span = bit_target->span;
                bit_instruction.type = minic_type_unsigned_int();
                bit_instruction.result = MINIC_CORE_VALUE_INVALID;
                bit_instruction.value.integer_value = (int64_t)bit_offset;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &bit_instruction, &bit_constant)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                (void)memset(&bit_instruction, 0, sizeof(bit_instruction));
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT;
                bit_instruction.span = bit_target->span;
                bit_instruction.type = bit_storage_type;
                bit_instruction.result = MINIC_CORE_VALUE_INVALID;
                bit_instruction.value.binary.left = bit_current;
                bit_instruction.value.binary.right = bit_constant;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &bit_instruction, &bit_current)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
            }
            bit_low_mask = bit_field->bit_width == 64U
                               ? UINT64_MAX
                               : ((UINT64_C(1) << bit_field->bit_width) - UINT64_C(1));
            if (bit_field->bit_width < bit_storage_width) {
                (void)memset(&bit_instruction, 0, sizeof(bit_instruction));
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
                bit_instruction.span = bit_target->span;
                bit_instruction.type = bit_storage_type;
                bit_instruction.result = MINIC_CORE_VALUE_INVALID;
                (void)memcpy(&bit_instruction.value.integer_value,
                             &bit_low_mask,
                             sizeof(bit_low_mask));
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &bit_instruction, &bit_constant)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                (void)memset(&bit_instruction, 0, sizeof(bit_instruction));
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND;
                bit_instruction.span = bit_target->span;
                bit_instruction.type = bit_storage_type;
                bit_instruction.result = MINIC_CORE_VALUE_INVALID;
                bit_instruction.value.binary.left = bit_current;
                bit_instruction.value.binary.right = bit_constant;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &bit_instruction, &bit_current)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
            }
            if (!minic_type_equal(bit_storage_type, bit_value_type)) {
                bit_status = minic_core_lower_internal_append_integer_conversion(context,
                                                       bit_target->span,
                                                       bit_value_type,
                                                       bit_current,
                                                       &bit_current);
                if (bit_status != MINIC_CORE_LOWER_OK) {
                    return bit_status;
                }
            }
            if (minic_type_is_signed_integer(bit_value_type) &&
                bit_field->bit_width < bit_semantic_width) {
                MinicCoreValueId bit_sign_shift;
                uint64_t bit_sign_shift_bits =
                    (uint64_t)(bit_semantic_width - bit_field->bit_width);

                (void)memset(&bit_instruction, 0, sizeof(bit_instruction));
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
                bit_instruction.span = bit_target->span;
                bit_instruction.type = minic_type_unsigned_int();
                bit_instruction.result = MINIC_CORE_VALUE_INVALID;
                (void)memcpy(&bit_instruction.value.integer_value,
                             &bit_sign_shift_bits,
                             sizeof(bit_sign_shift_bits));
                if (!minic_core_function_append_value_instruction(
                        context->function,
                        context->block_id,
                        &bit_instruction,
                        &bit_sign_shift)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                (void)memset(&bit_instruction, 0, sizeof(bit_instruction));
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT;
                bit_instruction.span = bit_target->span;
                bit_instruction.type = bit_value_type;
                bit_instruction.result = MINIC_CORE_VALUE_INVALID;
                bit_instruction.value.binary.left = bit_current;
                bit_instruction.value.binary.right = bit_sign_shift;
                if (!minic_core_function_append_value_instruction(
                        context->function,
                        context->block_id,
                        &bit_instruction,
                        &bit_current)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                (void)memset(&bit_instruction, 0, sizeof(bit_instruction));
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT;
                bit_instruction.span = bit_target->span;
                bit_instruction.type = bit_value_type;
                bit_instruction.result = MINIC_CORE_VALUE_INVALID;
                bit_instruction.value.binary.left = bit_current;
                bit_instruction.value.binary.right = bit_sign_shift;
                if (!minic_core_function_append_value_instruction(
                        context->function,
                        context->block_id,
                        &bit_instruction,
                        &bit_current)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
            }
            bit_status = minic_core_lower_internal_append_integer_conversion(context,
                                                   bit_target->span,
                                                   bit_common_type,
                                                   bit_current,
                                                   &bit_current_common);
            if (bit_status != MINIC_CORE_LOWER_OK) {
                return bit_status;
            }
            bit_address_type = context->function->values[bit_address].type;
            bit_status = minic_core_lower_internal_spill_scalar_value(
                context, bit_target->span, bit_address_type, bit_address, &bit_address_object);
            if (bit_status != MINIC_CORE_LOWER_OK) {
                return bit_status;
            }
            bit_status = minic_core_lower_internal_spill_scalar_value(context,
                                            bit_target->span,
                                            bit_common_type,
                                            bit_current_common,
                                            &bit_current_object);
            if (bit_status != MINIC_CORE_LOWER_OK) {
                return bit_status;
            }
            bit_status = lower_expression(
                context, expression->value.binary.right, &bit_rhs);
            if (bit_status != MINIC_CORE_LOWER_OK) {
                return bit_status;
            }
            bit_status = minic_core_lower_internal_append_integer_conversion(
                context, bit_source->span, bit_right_type, bit_rhs, &bit_rhs_common);
            if (bit_status != MINIC_CORE_LOWER_OK) {
                return bit_status;
            }
            bit_status = minic_core_lower_internal_reload_scalar_value(context,
                                             bit_target->span,
                                             bit_common_type,
                                             bit_current_object,
                                             &bit_current_common);
            if (bit_status != MINIC_CORE_LOWER_OK) {
                return bit_status;
            }
            bit_status = minic_core_lower_internal_reload_scalar_value(context,
                                             bit_target->span,
                                             bit_address_type,
                                             bit_address_object,
                                             &bit_address);
            if (bit_status != MINIC_CORE_LOWER_OK) {
                return bit_status;
            }
            (void)memset(&bit_instruction, 0, sizeof(bit_instruction));
            switch (expression->value.binary.operator_kind) {
            case MINIC_BINARY_ADD:
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_ADD;
                break;
            case MINIC_BINARY_SUBTRACT:
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT;
                break;
            case MINIC_BINARY_MULTIPLY:
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY;
                break;
            case MINIC_BINARY_DIVIDE:
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE;
                break;
            case MINIC_BINARY_REMAINDER:
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER;
                break;
            case MINIC_BINARY_SHIFT_LEFT:
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT;
                break;
            case MINIC_BINARY_SHIFT_RIGHT:
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT;
                break;
            case MINIC_BINARY_BITWISE_AND:
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND;
                break;
            case MINIC_BINARY_BITWISE_XOR:
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR;
                break;
            case MINIC_BINARY_BITWISE_OR:
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR;
                break;
            default:
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            bit_instruction.span = expression->span;
            bit_instruction.type = bit_common_type;
            bit_instruction.result = MINIC_CORE_VALUE_INVALID;
            bit_instruction.value.binary.left = bit_current_common;
            bit_instruction.value.binary.right = bit_rhs_common;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &bit_instruction, &bit_result)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            bit_status = minic_core_lower_internal_append_integer_conversion(context,
                                                   expression->span,
                                                   bit_value_type,
                                                   bit_result,
                                                   &bit_stored_value);
            if (bit_status != MINIC_CORE_LOWER_OK) {
                return bit_status;
            }
            if (minic_type_equal(bit_storage_type, bit_value_type)) {
                bit_field_storage = bit_stored_value;
            } else {
                bit_status = minic_core_lower_internal_append_integer_conversion(context,
                                                       expression->span,
                                                       bit_storage_type,
                                                       bit_stored_value,
                                                       &bit_field_storage);
                if (bit_status != MINIC_CORE_LOWER_OK) {
                    return bit_status;
                }
            }
            if (bit_field->bit_width < bit_storage_width) {
                (void)memset(&bit_instruction, 0, sizeof(bit_instruction));
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
                bit_instruction.span = expression->span;
                bit_instruction.type = bit_storage_type;
                bit_instruction.result = MINIC_CORE_VALUE_INVALID;
                (void)memcpy(&bit_instruction.value.integer_value,
                             &bit_low_mask,
                             sizeof(bit_low_mask));
                if (!minic_core_function_append_value_instruction(
                        context->function,
                        context->block_id,
                        &bit_instruction,
                        &bit_constant)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                (void)memset(&bit_instruction, 0, sizeof(bit_instruction));
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND;
                bit_instruction.span = expression->span;
                bit_instruction.type = bit_storage_type;
                bit_instruction.result = MINIC_CORE_VALUE_INVALID;
                bit_instruction.value.binary.left = bit_field_storage;
                bit_instruction.value.binary.right = bit_constant;
                if (!minic_core_function_append_value_instruction(
                        context->function,
                        context->block_id,
                        &bit_instruction,
                        &bit_field_storage)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
            }
            if (minic_type_is_signed_integer(bit_value_type)) {
                if (!minic_type_equal(bit_storage_type, bit_value_type)) {
                    bit_status = minic_core_lower_internal_append_integer_conversion(context,
                                                           expression->span,
                                                           bit_value_type,
                                                           bit_field_storage,
                                                           &bit_stored_value);
                    if (bit_status != MINIC_CORE_LOWER_OK) {
                        return bit_status;
                    }
                } else {
                    bit_stored_value = bit_field_storage;
                }
                if (bit_field->bit_width < bit_semantic_width) {
                    MinicCoreValueId bit_sign_shift;
                    uint64_t bit_sign_shift_bits =
                        (uint64_t)(bit_semantic_width - bit_field->bit_width);

                    (void)memset(&bit_instruction, 0, sizeof(bit_instruction));
                    bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
                    bit_instruction.span = expression->span;
                    bit_instruction.type = minic_type_unsigned_int();
                    bit_instruction.result = MINIC_CORE_VALUE_INVALID;
                    (void)memcpy(&bit_instruction.value.integer_value,
                                 &bit_sign_shift_bits,
                                 sizeof(bit_sign_shift_bits));
                    if (!minic_core_function_append_value_instruction(
                            context->function,
                            context->block_id,
                            &bit_instruction,
                            &bit_sign_shift)) {
                        return MINIC_CORE_LOWER_ERROR;
                    }
                    (void)memset(&bit_instruction, 0, sizeof(bit_instruction));
                    bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT;
                    bit_instruction.span = expression->span;
                    bit_instruction.type = bit_value_type;
                    bit_instruction.result = MINIC_CORE_VALUE_INVALID;
                    bit_instruction.value.binary.left = bit_stored_value;
                    bit_instruction.value.binary.right = bit_sign_shift;
                    if (!minic_core_function_append_value_instruction(
                            context->function,
                            context->block_id,
                            &bit_instruction,
                            &bit_stored_value)) {
                        return MINIC_CORE_LOWER_ERROR;
                    }
                    (void)memset(&bit_instruction, 0, sizeof(bit_instruction));
                    bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT;
                    bit_instruction.span = expression->span;
                    bit_instruction.type = bit_value_type;
                    bit_instruction.result = MINIC_CORE_VALUE_INVALID;
                    bit_instruction.value.binary.left = bit_stored_value;
                    bit_instruction.value.binary.right = bit_sign_shift;
                    if (!minic_core_function_append_value_instruction(
                            context->function,
                            context->block_id,
                            &bit_instruction,
                            &bit_stored_value)) {
                        return MINIC_CORE_LOWER_ERROR;
                    }
                }
            }
            if (bit_offset != 0U) {
                uint64_t bit_shift = (uint64_t)bit_offset;
                (void)memset(&bit_instruction, 0, sizeof(bit_instruction));
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
                bit_instruction.span = expression->span;
                bit_instruction.type = bit_storage_type;
                bit_instruction.result = MINIC_CORE_VALUE_INVALID;
                (void)memcpy(&bit_instruction.value.integer_value,
                             &bit_shift,
                             sizeof(bit_shift));
                if (!minic_core_function_append_value_instruction(
                        context->function,
                        context->block_id,
                        &bit_instruction,
                        &bit_constant)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                (void)memset(&bit_instruction, 0, sizeof(bit_instruction));
                bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT;
                bit_instruction.span = expression->span;
                bit_instruction.type = bit_storage_type;
                bit_instruction.result = MINIC_CORE_VALUE_INVALID;
                bit_instruction.value.binary.left = bit_field_storage;
                bit_instruction.value.binary.right = bit_constant;
                if (!minic_core_function_append_value_instruction(
                        context->function,
                        context->block_id,
                        &bit_instruction,
                        &bit_field_storage)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
            }
            bit_field_mask = bit_low_mask << bit_offset;
            bit_storage_mask = bit_storage_width == 64U
                                   ? UINT64_MAX
                                   : ((UINT64_C(1) << bit_storage_width) - UINT64_C(1));
            bit_clear_mask = (~bit_field_mask) & bit_storage_mask;
            bit_status = core_bit_field_load_storage(
                context, expression->span, bit_address, bit_storage_type,
                bit_narrow, bit_byte_count,
                minic_type_is_volatile(bit_target->type), &bit_merge_current);
            if (bit_status != MINIC_CORE_LOWER_OK) return bit_status;
            (void)memset(&bit_instruction, 0, sizeof(bit_instruction));
            bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
            bit_instruction.span = expression->span;
            bit_instruction.type = bit_storage_type;
            bit_instruction.result = MINIC_CORE_VALUE_INVALID;
            (void)memcpy(&bit_instruction.value.integer_value,
                         &bit_clear_mask,
                         sizeof(bit_clear_mask));
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &bit_instruction, &bit_constant)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&bit_instruction, 0, sizeof(bit_instruction));
            bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND;
            bit_instruction.span = expression->span;
            bit_instruction.type = bit_storage_type;
            bit_instruction.result = MINIC_CORE_VALUE_INVALID;
            bit_instruction.value.binary.left = bit_merge_current;
            bit_instruction.value.binary.right = bit_constant;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &bit_instruction, &bit_merged)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&bit_instruction, 0, sizeof(bit_instruction));
            bit_instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR;
            bit_instruction.span = expression->span;
            bit_instruction.type = bit_storage_type;
            bit_instruction.result = MINIC_CORE_VALUE_INVALID;
            bit_instruction.value.binary.left = bit_merged;
            bit_instruction.value.binary.right = bit_field_storage;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &bit_instruction, &bit_merged)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            bit_status = core_bit_field_store_storage(
                context, expression->span, bit_address, bit_merged,
                bit_narrow, bit_byte_count,
                minic_type_is_volatile(bit_target->type));
            if (bit_status != MINIC_CORE_LOWER_OK) return bit_status;
            *value_id = bit_stored_value;
            return MINIC_CORE_LOWER_OK;
        }
    }
    return MINIC_CORE_LOWER_OK;
}

MinicCoreLowerStatus minic_core_lower_bit_field_update(
    MinicCoreLowerContext *context,
    const MinicExpression *expression,
    const MinicExpression *operand,
    bool increment,
    bool prefix,
    MinicCoreValueId *value_id) {
    const MinicExpression *base;
    const MinicRecord *record;
    const MinicRecordField *field;
    MinicCoreInstruction instruction;
    MinicCoreLowerStatus status;
    MinicCoreValueId address;
    MinicCoreValueId base_value;
    MinicCoreValueId constant;
    MinicCoreValueId current_field;
    MinicCoreValueId current_promoted;
    MinicCoreValueId current_storage;
    MinicCoreValueId field_storage;
    MinicCoreValueId merged;
    MinicCoreValueId shifted_current;
    MinicCoreValueId updated_promoted;
    MinicCoreValueId updated_value;
    MinicType base_value_type;
    MinicType expression_value_type;
    MinicType promoted_type;
    MinicType record_type;
    MinicType storage_access_type;
    MinicType storage_type;
    MinicType value_type;
    size_t byte_offset;
    size_t bit_offset;
    size_t byte_count;
    unsigned int storage_width;
    unsigned int semantic_width;
    bool narrow;
    uint64_t clear_mask;
    uint64_t field_mask;
    uint64_t low_mask;
    uint64_t storage_mask;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || expression == NULL || operand == NULL || value_id == NULL ||
        context->target == NULL || operand->kind != MINIC_EXPRESSION_MEMBER ||
        operand->value_category != MINIC_VALUE_LVALUE || minic_type_is_const(operand->type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    record = minic_c0_program_record(context->body->program, operand->value.member.record_id);
    field = minic_c0_record_field(record, operand->value.member.field_index);
    base = minic_c0_program_expression(context->body->program, operand->value.member.base);
    if (record == NULL || field == NULL || !field->is_bit_field || base == NULL ||
        field->bit_width == 0U ||
        !minic_type_unqualified(operand->type, &value_type) ||
        !minic_type_is_integer(value_type) ||
        (!core_unsigned_bit_field_semantic_type(context, value_type) &&
         !minic_type_is_signed_integer(value_type)) ||
        minic_type_is_bool_integer(value_type) ||
        !minic_type_unqualified(expression->type, &expression_value_type) ||
        !minic_type_equal(expression_value_type, value_type) ||
        !minic_target_info_integer_promotion_for_program(
            context->target, context->body->program, value_type, &promoted_type) ||
        !core_bit_field_storage_type(
            context, value_type, &storage_type, &storage_width) ||
        storage_width == 0U || storage_width > 64U || field->bit_width > storage_width ||
        !minic_data_layout_record_field_layout(core_data_layout(context),
                                               context->body->program,
                                               record,
                                               operand->value.member.field_index,
                                               &byte_offset,
                                               &bit_offset) ||
        !core_scalar_expression_value_type(context->body, base, &base_value_type) ||
        !minic_type_is_pointer(base_value_type) ||
        !minic_type_pointee(base_value_type, &record_type) ||
        !minic_type_is_record(record_type) ||
        record_type.record_id != operand->value.member.record_id) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    semantic_width = storage_width;
    if (!core_bit_field_access_window(
            context, record, operand->value.member.record_id,
            byte_offset, bit_offset, field->bit_width, &storage_width,
            &storage_type, &storage_access_type, &byte_count, &narrow))
        return MINIC_CORE_LOWER_UNSUPPORTED;
    if (minic_type_is_volatile(operand->type) &&
        !minic_type_add_volatile(storage_access_type, &storage_access_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    status = lower_expression(context, operand->value.member.base, &base_value);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (base_value >= context->function->value_count ||
        !minic_type_equal(context->function->values[base_value].type, base_value_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    status = minic_core_lower_internal_append_field_address(context,
                                  operand->span,
                                  base_value,
                                  operand->value.member.record_id,
                                  operand->value.member.field_index,
                                  storage_access_type,
                                  &address);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    status = core_bit_field_load_storage(
        context, operand->span, address, storage_type, narrow, byte_count,
        minic_type_is_volatile(operand->type), &current_storage);
    if (status != MINIC_CORE_LOWER_OK) return status;
    shifted_current = current_storage;
    if (bit_offset != 0U) {
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        instruction.span = operand->span;
        instruction.type = minic_type_unsigned_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.integer_value = (int64_t)bit_offset;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &constant)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT;
        instruction.span = operand->span;
        instruction.type = storage_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = shifted_current;
        instruction.value.binary.right = constant;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &shifted_current)) {
            return MINIC_CORE_LOWER_ERROR;
        }
    }
    low_mask = field->bit_width == 64U
                   ? UINT64_MAX
                   : ((UINT64_C(1) << field->bit_width) - UINT64_C(1));
    if (field->bit_width < storage_width) {
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        instruction.span = operand->span;
        instruction.type = storage_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        (void)memcpy(&instruction.value.integer_value, &low_mask, sizeof(low_mask));
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &constant)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND;
        instruction.span = operand->span;
        instruction.type = storage_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = shifted_current;
        instruction.value.binary.right = constant;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &shifted_current)) {
            return MINIC_CORE_LOWER_ERROR;
        }
    }
    current_field = shifted_current;
    if (!minic_type_equal(storage_type, value_type)) {
        status = minic_core_lower_internal_append_integer_conversion(
            context, operand->span, value_type, current_field, &current_field);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    }
    /* M154_SIGNED_BIT_FIELD_UPDATE_OWNER: the extracted storage bits are an
       unsigned bit pattern.  Reconstruct the signed field value before integer
       promotion, matching M103 signed bit-field reads and preserving postfix
       result semantics. */
    if (minic_type_is_signed_integer(value_type) && field->bit_width < semantic_width) {
        MinicCoreValueId sign_shift;
        uint64_t sign_shift_bits = (uint64_t)(semantic_width - field->bit_width);

        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        instruction.span = operand->span;
        instruction.type = minic_type_unsigned_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        (void)memcpy(&instruction.value.integer_value, &sign_shift_bits, sizeof(sign_shift_bits));
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &sign_shift)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT;
        instruction.span = operand->span;
        instruction.type = value_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = current_field;
        instruction.value.binary.right = sign_shift;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &current_field)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT;
        instruction.span = operand->span;
        instruction.type = value_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = current_field;
        instruction.value.binary.right = sign_shift;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &current_field)) {
            return MINIC_CORE_LOWER_ERROR;
        }
    }
    status = minic_core_lower_internal_append_integer_conversion(
        context, operand->span, promoted_type, current_field, &current_promoted);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
    instruction.span = expression->span;
    instruction.type = promoted_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.integer_value = 1;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &instruction, &constant)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = increment ? MINIC_CORE_INSTRUCTION_INTEGER_ADD
                                 : MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT;
    instruction.span = expression->span;
    instruction.type = promoted_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.binary.left = current_promoted;
    instruction.value.binary.right = constant;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &instruction, &updated_promoted)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    status = minic_core_lower_internal_append_integer_conversion(
        context, expression->span, value_type, updated_promoted, &updated_value);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    field_storage = updated_value;
    if (!minic_type_equal(value_type, storage_type)) {
        status = minic_core_lower_internal_append_integer_conversion(
            context, expression->span, storage_type, field_storage, &field_storage);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    }
    if (field->bit_width < storage_width) {
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        instruction.span = expression->span;
        instruction.type = storage_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        (void)memcpy(&instruction.value.integer_value, &low_mask, sizeof(low_mask));
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &constant)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND;
        instruction.span = expression->span;
        instruction.type = storage_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = field_storage;
        instruction.value.binary.right = constant;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &field_storage)) {
            return MINIC_CORE_LOWER_ERROR;
        }
    }
    /* Prefix update yields the value actually stored in the bit-field, not an
       untruncated arithmetic temporary.  Rebuild that value from the masked
       storage bits; signed fields then use the same width sign extension. */
    if (minic_type_equal(storage_type, value_type)) {
        updated_value = field_storage;
    } else {
        status = minic_core_lower_internal_append_integer_conversion(
            context, expression->span, value_type, field_storage, &updated_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    }
    if (minic_type_is_signed_integer(value_type) && field->bit_width < semantic_width) {
        MinicCoreValueId sign_shift;
        uint64_t sign_shift_bits = (uint64_t)(semantic_width - field->bit_width);

        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        instruction.span = expression->span;
        instruction.type = minic_type_unsigned_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        (void)memcpy(&instruction.value.integer_value, &sign_shift_bits, sizeof(sign_shift_bits));
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &sign_shift)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT;
        instruction.span = expression->span;
        instruction.type = value_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = updated_value;
        instruction.value.binary.right = sign_shift;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &updated_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT;
        instruction.span = expression->span;
        instruction.type = value_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = updated_value;
        instruction.value.binary.right = sign_shift;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &updated_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }
    }
    if (bit_offset != 0U) {
        uint64_t shift = (uint64_t)bit_offset;
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
        instruction.span = expression->span;
        instruction.type = storage_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        (void)memcpy(&instruction.value.integer_value, &shift, sizeof(shift));
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &constant)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT;
        instruction.span = expression->span;
        instruction.type = storage_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = field_storage;
        instruction.value.binary.right = constant;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &field_storage)) {
            return MINIC_CORE_LOWER_ERROR;
        }
    }

    field_mask = low_mask << bit_offset;
    storage_mask = storage_width == 64U
                       ? UINT64_MAX
                       : ((UINT64_C(1) << storage_width) - UINT64_C(1));
    clear_mask = (~field_mask) & storage_mask;
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
    instruction.span = expression->span;
    instruction.type = storage_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    (void)memcpy(&instruction.value.integer_value, &clear_mask, sizeof(clear_mask));
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &instruction, &constant)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND;
    instruction.span = expression->span;
    instruction.type = storage_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.binary.left = current_storage;
    instruction.value.binary.right = constant;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &instruction, &merged)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR;
    instruction.span = expression->span;
    instruction.type = storage_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.binary.left = merged;
    instruction.value.binary.right = field_storage;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &instruction, &merged)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    status = core_bit_field_store_storage(
        context, expression->span, address, merged, narrow, byte_count,
        minic_type_is_volatile(operand->type));
    if (status != MINIC_CORE_LOWER_OK) return status;
    *value_id = prefix ? updated_value : current_field;
    return MINIC_CORE_LOWER_OK;
}

