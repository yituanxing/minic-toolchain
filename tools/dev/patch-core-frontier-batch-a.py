#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    if new in text:
        return
    if old not in text:
        raise SystemExit(f"anchor not found in {path}")
    p.write_text(text.replace(old, new, 1))


# Batch A/1: unsigned integer bit-field reads are ordinary scalar values, but
# their storage address is not a C-addressable lvalue.  Keep address-of/write
# fail-closed; only the read path may form the internal storage-unit address.
path = "src/core/core_lower.c"
old = '''    if (expression->value_category == MINIC_VALUE_LVALUE &&
        core_memory_scalar_type(expression->type)) {
'''
new = '''    /* BATCH_A_UNSIGNED_BIT_FIELD_READ: a bit-field is not C-addressable,
       but reading it is a scalar operation.  Form the storage-unit address
       internally, load the declared integer unit, then extract the field.
       Signed bit-fields and bit-field writes remain fail-closed for a later
       semantic batch; this seam is generic for unsigned integer bit-fields. */
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
        MinicType value_type;
        size_t byte_offset;
        size_t bit_offset;
        unsigned int storage_width;
        uint64_t mask_bits;

        base = minic_c0_program_expression(context->body->program, expression->value.member.base);
        record = minic_c0_program_record(context->body->program, expression->value.member.record_id);
        field = minic_c0_record_field(record, expression->value.member.field_index);
        if (field != NULL && field->is_bit_field) {
            if (base == NULL || record == NULL || field->bit_width == 0U ||
                !minic_type_unqualified(expression->type, &value_type) ||
                !minic_type_is_integer(value_type) ||
                !minic_type_is_unsigned_integer(value_type) ||
                context->target == NULL ||
                !minic_target_info_integer_width(
                    context->target, context->body->program, value_type, &storage_width) ||
                storage_width == 0U || storage_width > 64U ||
                field->bit_width > storage_width ||
                !minic_data_layout_record_field_layout(minic_default_data_layout(),
                                                       context->body->program,
                                                       record,
                                                       expression->value.member.field_index,
                                                       &byte_offset,
                                                       &bit_offset) ||
                bit_offset + field->bit_width > storage_width ||
                !core_scalar_expression_value_type(context->body, base, &base_value_type) ||
                !minic_type_is_pointer(base_value_type) ||
                !minic_type_pointee(base_value_type, &record_type) ||
                !minic_type_is_record(record_type) ||
                record_type.record_id != expression->value.member.record_id) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            (void)byte_offset;
            status = lower_expression(context, expression->value.member.base, &base_id);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            if (base_id >= context->function->value_count ||
                !minic_type_equal(context->function->values[base_id].type, base_value_type)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            status = append_field_address(context,
                                          expression->span,
                                          base_id,
                                          expression->value.member.record_id,
                                          expression->value.member.field_index,
                                          expression->type,
                                          &address_id);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            (void)memset(&extract, 0, sizeof(extract));
            extract.kind = MINIC_CORE_INSTRUCTION_LOAD;
            extract.span = expression->span;
            extract.type = value_type;
            extract.result = MINIC_CORE_VALUE_INVALID;
            extract.value.load.address = address_id;
            extract.value.load.is_volatile = minic_type_is_volatile(expression->type);
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &extract, &current)) {
                return MINIC_CORE_LOWER_ERROR;
            }
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
                extract.type = value_type;
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
                extract.type = value_type;
                extract.result = MINIC_CORE_VALUE_INVALID;
                (void)memcpy(&extract.value.integer_value, &mask_bits, sizeof(mask_bits));
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &extract, &rhs)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
                (void)memset(&extract, 0, sizeof(extract));
                extract.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND;
                extract.span = expression->span;
                extract.type = value_type;
                extract.result = MINIC_CORE_VALUE_INVALID;
                extract.value.binary.left = current;
                extract.value.binary.right = rhs;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &extract, &current)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
            }
            *value_id = current;
            return MINIC_CORE_LOWER_OK;
        }
    }
    if (expression->value_category == MINIC_VALUE_LVALUE &&
        core_memory_scalar_type(expression->type)) {
'''
replace_once(path, old, new)

# The internal FIELD_ADDRESS used by the read above addresses the storage unit;
# C-level address-of still stays rejected by lower_address().
path = "src/target/riscv64/core_codegen.c"
old = '''    if (record == NULL || field == NULL || field->is_bit_field ||
        !minic_data_layout_record_field_offset(minic_default_data_layout(),
'''
new = '''    /* BATCH_A_UNSIGNED_BIT_FIELD_READ: FIELD_ADDRESS may be used internally
       for a bit-field storage-unit read.  The frontend/Core lowerer still
       rejects taking a C address of a bit-field. */
    if (record == NULL || field == NULL ||
        !minic_data_layout_record_field_offset(minic_default_data_layout(),
'''
replace_once(path, old, new)

# Batch A/2: top-level qualifiers on a by-value record parameter describe the
# callee's local parameter object, not the ABI value type.  Verify the ingress
# object by its unqualified value type while preserving qualified local storage.
path = "src/core/core_ir.c"
old = '''    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT:
        return instruction->result == MINIC_CORE_VALUE_INVALID &&
               minic_type_is_void(instruction->type) &&
               instruction->value.parameter_object.parameter_index < function->parameter_count &&
               instruction->value.parameter_object.object_id < function->object_count &&
               minic_type_is_record(
                   function
                       ->parameter_types[instruction->value.parameter_object.parameter_index]) &&
               minic_type_equal(
                   function->parameter_types[instruction->value.parameter_object.parameter_index],
                   function->objects[instruction->value.parameter_object.object_id].type);
'''
new = '''    case MINIC_CORE_INSTRUCTION_PARAMETER_OBJECT: {
        MinicCoreObjectId object_id;
        MinicType object_value_type;
        size_t parameter_index;

        object_id = instruction->value.parameter_object.object_id;
        parameter_index = instruction->value.parameter_object.parameter_index;
        return instruction->result == MINIC_CORE_VALUE_INVALID &&
               minic_type_is_void(instruction->type) &&
               parameter_index < function->parameter_count && object_id < function->object_count &&
               minic_type_is_record(function->parameter_types[parameter_index]) &&
               minic_type_unqualified(function->objects[object_id].type, &object_value_type) &&
               minic_type_equal(function->parameter_types[parameter_index], object_value_type);
    }
'''
replace_once(path, old, new)

# The RV64 aggregate-parameter materializer must use the same semantic rule as
# the Core verifier; qualifiers do not change ABI placement or object size.
path = "src/target/riscv64/core_codegen.c"
old = '''static bool emit_parameter_object(FILE *file,
                                  const MinicC0Program *program,
                                  const MinicCoreFunction *function,
                                  const MinicRiscv64CoreFrame *frame,
                                  const MinicCoreInstruction *instruction) {
    MinicRiscv64AbiArgumentLocation location;
    MinicCoreObjectId object_id;
    size_t object_offset;
    size_t chunk_index;
'''
new = '''static bool emit_parameter_object(FILE *file,
                                  const MinicC0Program *program,
                                  const MinicCoreFunction *function,
                                  const MinicRiscv64CoreFrame *frame,
                                  const MinicCoreInstruction *instruction) {
    MinicRiscv64AbiArgumentLocation location;
    MinicCoreObjectId object_id;
    MinicType object_value_type;
    size_t object_offset;
    size_t chunk_index;
'''
replace_once(path, old, new)
old = '''        object_id >= function->object_count ||
        !minic_type_equal(
            function->objects[object_id].type,
            function->parameter_types[instruction->value.parameter_object.parameter_index]) ||
        !core_object_offset(program, function, object_id, &object_offset)) {
'''
new = '''        object_id >= function->object_count ||
        !minic_type_unqualified(function->objects[object_id].type, &object_value_type) ||
        !minic_type_equal(
            object_value_type,
            function->parameter_types[instruction->value.parameter_object.parameter_index]) ||
        !core_object_offset(program, function, object_id, &object_offset)) {
'''
replace_once(path, old, new)

print("CORE_BATCH_A_PATCHED bitfield-read qualified-record-parameter")
