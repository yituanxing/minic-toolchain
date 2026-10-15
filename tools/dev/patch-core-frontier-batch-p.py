#!/usr/bin/env python3
from pathlib import Path

path = Path("src/core/core_lower.c")
text = path.read_text()
anchor = '''    if (!minic_type_unqualified(target->type, &stored_type) ||
        !core_memory_scalar_type(stored_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
'''
insert = r'''    /* BATCH_P_UNSIGNED_BIT_FIELD_WRITE: bit-fields are not C-addressable, so
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
            MinicCoreInstruction operation;
            MinicCoreObjectId source_object;
            MinicCoreValueId address;
            MinicCoreValueId base_value;
            MinicCoreValueId current;
            MinicCoreValueId field_value;
            MinicCoreValueId constant;
            MinicCoreValueId merged;
            MinicCoreLowerStatus bit_status;
            MinicType base_value_type;
            MinicType record_type;
            MinicType value_type;
            size_t byte_offset;
            size_t bit_offset;
            unsigned int storage_width;
            uint64_t low_mask;
            uint64_t field_mask;
            uint64_t clear_mask;
            uint64_t storage_mask;

            if (base == NULL || record == NULL || field->bit_width == 0U ||
                !minic_type_unqualified(target->type, &value_type) ||
                !minic_type_is_integer(value_type) ||
                !minic_type_is_unsigned_integer(value_type) ||
                minic_type_is_const(target->type) || context->target == NULL ||
                !minic_target_info_integer_width(
                    context->target, context->body->program, value_type, &storage_width) ||
                storage_width == 0U || storage_width > 64U ||
                field->bit_width > storage_width ||
                !minic_data_layout_record_field_layout(minic_default_data_layout(),
                                                       context->body->program,
                                                       record,
                                                       target->value.member.field_index,
                                                       &byte_offset,
                                                       &bit_offset) ||
                bit_offset + field->bit_width > storage_width ||
                !core_scalar_expression_value_type(context->body, base, &base_value_type) ||
                !minic_type_is_pointer(base_value_type) ||
                !minic_type_pointee(base_value_type, &record_type) ||
                !minic_type_is_record(record_type) ||
                record_type.record_id != target->value.member.record_id) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            (void)byte_offset;

            bit_status = lower_scalar_assignment_value(
                context, value_type, source_id, &field_value);
            if (bit_status != MINIC_CORE_LOWER_OK) {
                return bit_status;
            }
            bit_status = spill_scalar_value(
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
            bit_status = append_field_address(context,
                                              target->span,
                                              base_value,
                                              target->value.member.record_id,
                                              target->value.member.field_index,
                                              target->type,
                                              &address);
            if (bit_status != MINIC_CORE_LOWER_OK) {
                return bit_status;
            }
            (void)memset(&operation, 0, sizeof(operation));
            operation.kind = MINIC_CORE_INSTRUCTION_LOAD;
            operation.span = target->span;
            operation.type = value_type;
            operation.result = MINIC_CORE_VALUE_INVALID;
            operation.value.load.address = address;
            operation.value.load.is_volatile = minic_type_is_volatile(target->type);
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &operation, &current)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            bit_status = reload_scalar_value(
                context, span, value_type, source_object, &field_value);
            if (bit_status != MINIC_CORE_LOWER_OK) {
                return bit_status;
            }

            low_mask = field->bit_width == 64U
                           ? UINT64_MAX
                           : ((UINT64_C(1) << field->bit_width) - UINT64_C(1));
            if (field->bit_width < storage_width) {
                (void)memset(&operation, 0, sizeof(operation));
                operation.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
                operation.span = span;
                operation.type = value_type;
                operation.result = MINIC_CORE_VALUE_INVALID;
                (void)memcpy(&operation.value.integer_value, &low_mask, sizeof(low_mask));
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &operation, &constant)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                (void)memset(&operation, 0, sizeof(operation));
                operation.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND;
                operation.span = span;
                operation.type = value_type;
                operation.result = MINIC_CORE_VALUE_INVALID;
                operation.value.binary.left = field_value;
                operation.value.binary.right = constant;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &operation, &field_value)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
            }
            if (bit_offset != 0U) {
                uint64_t shift_bits = (uint64_t)bit_offset;

                (void)memset(&operation, 0, sizeof(operation));
                operation.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
                operation.span = span;
                operation.type = value_type;
                operation.result = MINIC_CORE_VALUE_INVALID;
                (void)memcpy(&operation.value.integer_value, &shift_bits, sizeof(shift_bits));
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &operation, &constant)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                (void)memset(&operation, 0, sizeof(operation));
                operation.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT;
                operation.span = span;
                operation.type = value_type;
                operation.result = MINIC_CORE_VALUE_INVALID;
                operation.value.binary.left = field_value;
                operation.value.binary.right = constant;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &operation, &field_value)) {
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
            operation.type = value_type;
            operation.result = MINIC_CORE_VALUE_INVALID;
            (void)memcpy(&operation.value.integer_value, &clear_mask, sizeof(clear_mask));
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &operation, &constant)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&operation, 0, sizeof(operation));
            operation.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND;
            operation.span = span;
            operation.type = value_type;
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
            operation.type = value_type;
            operation.result = MINIC_CORE_VALUE_INVALID;
            operation.value.binary.left = merged;
            operation.value.binary.right = field_value;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &operation, &merged)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&operation, 0, sizeof(operation));
            operation.kind = MINIC_CORE_INSTRUCTION_STORE;
            operation.span = span;
            operation.type = minic_type_void();
            operation.result = MINIC_CORE_VALUE_INVALID;
            operation.value.store.address = address;
            operation.value.store.stored_value = merged;
            operation.value.store.is_volatile = minic_type_is_volatile(target->type);
            return minic_core_function_append_effect_instruction(
                       context->function, context->block_id, &operation)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
    }
'''
count = text.count(anchor)
if count != 1:
    raise SystemExit(f"Batch P assignment anchor count={count}")
path.write_text(text.replace(anchor, insert + anchor, 1))
print("CORE_BATCH_P_PATCHED unsigned bit-field assignment RMW")
