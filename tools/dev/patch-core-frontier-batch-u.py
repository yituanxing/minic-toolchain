#!/usr/bin/env python3
from pathlib import Path

path = Path("src/core/core_lower.c")
text = path.read_text()

old_proto = '''static MinicCoreLowerStatus lower_direct_record_call_object(
    MinicCoreLowerContext *context,
    const MinicExpression *expression,
    MinicCoreObjectId *result_object);
'''
new_proto = '''static MinicCoreLowerStatus lower_direct_record_call_object(
    MinicCoreLowerContext *context,
    const MinicExpression *expression,
    MinicCoreObjectId *result_object);
static MinicCoreLowerStatus lower_record_compound_literal_object(
    MinicCoreLowerContext *context,
    const MinicExpression *expression,
    MinicCoreObjectId *object_id);
'''
old_address = '''    if (expression->value_category != MINIC_VALUE_LVALUE) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
'''
new_address = '''    if (expression->value_category != MINIC_VALUE_LVALUE) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    /* BATCH_U_RECORD_COMPOUND_LITERAL_ADDRESS: a record compound literal is
       an lvalue with a real semantic backing object.  Reuse that object for
       address-of just as the address-backed aggregate seam already does; do
       not synthesize a second temporary and do not special-case call sites. */
    if (expression->kind == MINIC_EXPRESSION_COMPOUND_LITERAL &&
        minic_type_is_record(expression->type)) {
        status = lower_record_compound_literal_object(context, expression, &object_id);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_OBJECT_ADDRESS;
        instruction.span = expression->span;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.object_id = object_id;
        if (!minic_type_pointer_to(expression->type, &instruction.type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, address_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
'''

old_pointer = '''        MinicCoreValueId pointer_value;
        MinicCoreValueId index_value;
        MinicCoreLowerStatus status;
        size_t element_size;

        left_expression =
            minic_c0_program_expression(context->body->program, expression->value.binary.left);
        right_expression =
            minic_c0_program_expression(context->body->program, expression->value.binary.right);
        if (left_expression == NULL || right_expression == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (minic_type_is_pointer(left_expression->type) &&
            minic_type_is_integer(right_expression->type)) {
            pointer_expression = left_expression;
            index_expression = right_expression;
            pointer_id = expression->value.binary.left;
            index_id = expression->value.binary.right;
        } else if (expression->value.binary.operator_kind == MINIC_BINARY_ADD &&
                   minic_type_is_integer(left_expression->type) &&
                   minic_type_is_pointer(right_expression->type)) {
            pointer_expression = right_expression;
            index_expression = left_expression;
            pointer_id = expression->value.binary.right;
            index_id = expression->value.binary.left;
        } else {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        if (!minic_type_equal(pointer_expression->type, expression->type) ||
            !minic_c0_pointer_arithmetic_element_size(context->body->program,
                                                      minic_default_data_layout(),
                                                      expression->type,
                                                      &element_size)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_expression(context, pointer_id, &pointer_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = spill_scalar_value(context,
                                    pointer_expression->span,
                                    pointer_expression->type,
                                    pointer_value,
                                    &pointer_object);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = lower_expression(context, index_id, &index_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = reload_scalar_value(context,
                                     pointer_expression->span,
                                     pointer_expression->type,
                                     pointer_object,
                                     &pointer_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (pointer_value >= context->function->value_count ||
            index_value >= context->function->value_count ||
            !minic_type_equal(context->function->values[pointer_value].type,
                              pointer_expression->type) ||
            !minic_type_equal(context->function->values[index_value].type,
                              index_expression->type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
'''
new_pointer = '''        MinicCoreValueId pointer_value;
        MinicCoreValueId index_value;
        MinicCoreLowerStatus status;
        MinicType pointer_value_type;
        MinicType index_value_type;
        size_t element_size;

        left_expression =
            minic_c0_program_expression(context->body->program, expression->value.binary.left);
        right_expression =
            minic_c0_program_expression(context->body->program, expression->value.binary.right);
        if (left_expression == NULL || right_expression == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (minic_type_is_pointer(left_expression->type) &&
            minic_type_is_integer(right_expression->type)) {
            pointer_expression = left_expression;
            index_expression = right_expression;
            pointer_id = expression->value.binary.left;
            index_id = expression->value.binary.right;
        } else if (expression->value.binary.operator_kind == MINIC_BINARY_ADD &&
                   minic_type_is_integer(left_expression->type) &&
                   minic_type_is_pointer(right_expression->type)) {
            pointer_expression = right_expression;
            index_expression = left_expression;
            pointer_id = expression->value.binary.right;
            index_id = expression->value.binary.left;
        } else {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        /* BATCH_U_POINTER_ARITH_VALUE_TYPES: Core consumes scalar values, not
           lvalue storage qualifiers.  A member reached through `const T *`
           has a const-qualified lvalue type in the semantic AST, but its
           lvalue-to-rvalue result is the unqualified scalar value transported
           by Core.  Use the shared value-type seam for both operands instead
           of comparing emitted values against raw expression storage types. */
        if (!core_scalar_expression_value_type(
                context->body, pointer_expression, &pointer_value_type) ||
            !core_scalar_expression_value_type(
                context->body, index_expression, &index_value_type) ||
            !minic_type_is_pointer(pointer_value_type) ||
            !minic_type_is_integer(index_value_type) ||
            !minic_type_equal(pointer_value_type, expression->type) ||
            !minic_c0_pointer_arithmetic_element_size(context->body->program,
                                                      minic_default_data_layout(),
                                                      expression->type,
                                                      &element_size)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_expression(context, pointer_id, &pointer_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = spill_scalar_value(context,
                                    pointer_expression->span,
                                    pointer_value_type,
                                    pointer_value,
                                    &pointer_object);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = lower_expression(context, index_id, &index_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = reload_scalar_value(context,
                                     pointer_expression->span,
                                     pointer_value_type,
                                     pointer_object,
                                     &pointer_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (pointer_value >= context->function->value_count ||
            index_value >= context->function->value_count ||
            !minic_type_equal(context->function->values[pointer_value].type,
                              pointer_value_type) ||
            !minic_type_equal(context->function->values[index_value].type,
                              index_value_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
'''

already_address = "BATCH_U_RECORD_COMPOUND_LITERAL_ADDRESS" in text
already_pointer = "BATCH_U_POINTER_ARITH_VALUE_TYPES" in text
if not already_address:
    if old_proto not in text:
        raise SystemExit("Batch U prototype anchor not found")
    if old_address not in text:
        raise SystemExit("Batch U lower_address anchor not found")
    text = text.replace(old_proto, new_proto, 1)
    text = text.replace(old_address, new_address, 1)
if not already_pointer:
    if old_pointer not in text:
        raise SystemExit("Batch U pointer arithmetic anchor not found")
    text = text.replace(old_pointer, new_pointer, 1)
if already_address and already_pointer:
    print("CORE_BATCH_U_ALREADY_PATCHED")
else:
    path.write_text(text)
    print("CORE_BATCH_U_PATCHED compound address + scalar pointer arithmetic value types")
