#!/usr/bin/env python3
# M92-M94 hot batch: indirect-call diagnostics, pointer difference, and qualified member bases.

from pathlib import Path

PATH = Path("src/core/core_lower.c")
DETAIL_MARKER = "M92_INDIRECT_CALL_HOT_DETAIL"
POINTER_DIFF_MARKER = "M93_POINTER_DIFFERENCE"
MEMBER_BASE_MARKER = "M94_MEMBER_BASE_VALUE_TYPE"


def replace_once(text: str, old: str, new: str, name: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M92-M94 {name} anchor count={count}")
    return text.replace(old, new, 1)


def apply_detail(text: str) -> str:
    if DETAIL_MARKER in text:
        print("M92 indirect-call detail already applied")
        return text

    shape = '''    callee_expression =
        minic_c0_program_expression(context->body->program, expression->value.call.callee);
    if (callee_expression == NULL ||
        !core_scalar_expression_value_type(context->body, callee_expression, &callee_value_type) ||
        !minic_type_pointee(callee_value_type, &function_type) ||
        !minic_type_is_function(function_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
'''
    shape_new = '''    callee_expression =
        minic_c0_program_expression(context->body->program, expression->value.call.callee);
    if (callee_expression == NULL ||
        !core_scalar_expression_value_type(context->body, callee_expression, &callee_value_type) ||
        !minic_type_pointee(callee_value_type, &function_type) ||
        !minic_type_is_function(function_type)) {
        (void)fprintf(stderr,
                      "CORE_LOWER_DETAIL marker=M92_INDIRECT_CALL_HOT_DETAIL function=%s "
                      "stage=indirect-call reason=callee-shape callee_kind=%d\\n",
                      context->source_function != NULL ? context->source_function->name : "?",
                      callee_expression != NULL ? (int)callee_expression->kind : -1);
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
'''
    text = replace_once(text, shape, shape_new, "callee-shape")

    signature = '''    if (signature == NULL || signature->is_variadic ||
        expression->value.call.argument_count != signature->parameter_count ||
        !minic_type_equal(expression->type, signature->return_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
'''
    signature_new = '''    if (signature == NULL || signature->is_variadic ||
        expression->value.call.argument_count != signature->parameter_count ||
        !minic_type_equal(expression->type, signature->return_type)) {
        (void)fprintf(stderr,
                      "CORE_LOWER_DETAIL marker=M92_INDIRECT_CALL_HOT_DETAIL function=%s "
                      "stage=indirect-call reason=signature signature=%d variadic=%d argc=%zu expected=%zu return_match=%d\\n",
                      context->source_function != NULL ? context->source_function->name : "?",
                      signature != NULL ? 1 : 0,
                      signature != NULL && signature->is_variadic ? 1 : 0,
                      expression->value.call.argument_count,
                      signature != NULL ? signature->parameter_count : 0U,
                      signature != NULL && minic_type_equal(expression->type, signature->return_type) ? 1 : 0);
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
'''
    text = replace_once(text, signature, signature_new, "signature")

    callee_lower = '''    status = lower_expression(context, expression->value.call.callee, &callee_value);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
'''
    callee_lower_new = '''    status = lower_expression(context, expression->value.call.callee, &callee_value);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr,
                      "CORE_LOWER_DETAIL marker=M92_INDIRECT_CALL_HOT_DETAIL function=%s "
                      "stage=indirect-call reason=callee-lower status=%d callee_kind=%d\\n",
                      context->source_function != NULL ? context->source_function->name : "?",
                      (int)status,
                      callee_expression != NULL ? (int)callee_expression->kind : -1);
        return status;
    }
'''
    text = replace_once(text, callee_lower, callee_lower_new, "callee-lower")

    value_type = '''    if (callee_value >= context->function->value_count ||
        !minic_type_equal(context->function->values[callee_value].type,
                          callee_value_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
'''
    value_type_new = '''    if (callee_value >= context->function->value_count ||
        !minic_type_equal(context->function->values[callee_value].type,
                          callee_value_type)) {
        (void)fprintf(stderr,
                      "CORE_LOWER_DETAIL marker=M92_INDIRECT_CALL_HOT_DETAIL function=%s "
                      "stage=indirect-call reason=callee-value-type value=%u count=%zu\\n",
                      context->source_function != NULL ? context->source_function->name : "?",
                      (unsigned int)callee_value,
                      context->function->value_count);
        return MINIC_CORE_LOWER_ERROR;
    }
'''
    text = replace_once(text, value_type, value_type_new, "callee-value-type")
    print("M92 indirect-call hot detail applied")
    return text


def apply_pointer_difference(text: str) -> str:
    if POINTER_DIFF_MARKER in text:
        print("M93 pointer difference already applied")
        return text

    anchor = '''    /* M82_BINARY_POINTER_SUBTRACTION: C/GNU pointer +/- integer share the
       same scaled-offset primitive. Subtraction is only valid with the pointer
       on the left; integer - pointer remains fail-closed. */
'''
    block = r'''    /* M93_POINTER_DIFFERENCE: pointer - pointer produces an integer
       element distance. Compose existing target-neutral Core scalar primitives:
       pointer-to-integer bitcasts, byte subtraction, then signed division by
       the semantic pointee stride. */
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT &&
        minic_type_is_integer(expression->type)) {
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;
        MinicCoreLowerStatus status;
        MinicCoreObjectId left_object;
        MinicCoreValueId left_pointer;
        MinicCoreValueId right_pointer;
        MinicCoreValueId left_integer;
        MinicCoreValueId right_integer;
        MinicCoreValueId byte_difference;
        MinicCoreValueId divisor;
        MinicType left_type;
        MinicType right_type;
        size_t element_size;

        left_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        right_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.right);
        if (left_expression != NULL && right_expression != NULL &&
            core_scalar_expression_value_type(context->body, left_expression, &left_type) &&
            core_scalar_expression_value_type(context->body, right_expression, &right_type) &&
            minic_type_is_pointer(left_type) && minic_type_is_pointer(right_type) &&
            minic_type_equal(left_type, right_type) &&
            minic_c0_pointer_arithmetic_element_size(context->body->program,
                                                      minic_default_data_layout(),
                                                      left_type,
                                                      &element_size)) {
            status = lower_expression(
                context, expression->value.binary.left, &left_pointer);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            status = append_scalar_bitcast(
                context, left_expression->span, expression->type, left_pointer, &left_integer);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            status = spill_scalar_value(
                context, left_expression->span, expression->type, left_integer, &left_object);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            status = lower_expression(
                context, expression->value.binary.right, &right_pointer);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            status = append_scalar_bitcast(
                context, right_expression->span, expression->type, right_pointer, &right_integer);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            status = reload_scalar_value(
                context, left_expression->span, expression->type, left_object, &left_integer);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }

            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT;
            instruction.span = expression->span;
            instruction.type = expression->type;
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.binary.left = left_integer;
            instruction.value.binary.right = right_integer;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &instruction, &byte_difference)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (element_size == 1U) {
                *value_id = byte_difference;
                return MINIC_CORE_LOWER_OK;
            }

            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
            instruction.span = expression->span;
            instruction.type = expression->type;
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.integer_value = (int64_t)element_size;
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &instruction, &divisor)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE;
            instruction.span = expression->span;
            instruction.type = expression->type;
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.binary.left = byte_difference;
            instruction.value.binary.right = divisor;
            return minic_core_function_append_value_instruction(
                       context->function, context->block_id, &instruction, value_id)
                       ? MINIC_CORE_LOWER_OK
                       : MINIC_CORE_LOWER_ERROR;
        }
    }

'''
    text = replace_once(text, anchor, block + anchor, "pointer-difference")
    print("M93 pointer difference applied")
    return text


def apply_member_base_value_type(text: str) -> str:
    if MEMBER_BASE_MARKER in text:
        print("M94 member-base value type already applied")
        return text

    declarations = '''    if (expression->kind == MINIC_EXPRESSION_MEMBER) {
        const MinicExpression *base;
        const MinicRecord *record;
        const MinicRecordField *field;
        MinicCoreValueId base_id;
        MinicType record_type;
'''
    declarations_new = '''    if (expression->kind == MINIC_EXPRESSION_MEMBER) {
        const MinicExpression *base;
        const MinicRecord *record;
        const MinicRecordField *field;
        MinicCoreValueId base_id;
        MinicType base_value_type;
        MinicType record_type;
'''
    text = replace_once(text, declarations, declarations_new, "member-declarations")

    validation = '''        if (base == NULL || record == NULL || field == NULL || field->is_bit_field ||
            !minic_type_pointee(base->type, &record_type) || !minic_type_is_record(record_type) ||
            record_type.record_id != expression->value.member.record_id) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
'''
    validation_new = '''        /* M94_MEMBER_BASE_VALUE_TYPE: selecting a pointer member through
           `const struct *` qualifies the member lvalue storage, while evaluating
           it yields the unqualified pointer value. Nested member addressing must
           compare against that scalar value type, not the qualified lvalue type. */
        if (base == NULL || record == NULL || field == NULL || field->is_bit_field ||
            !core_scalar_expression_value_type(context->body, base, &base_value_type) ||
            !minic_type_is_pointer(base_value_type) ||
            !minic_type_pointee(base_value_type, &record_type) ||
            !minic_type_is_record(record_type) ||
            record_type.record_id != expression->value.member.record_id) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
'''
    text = replace_once(text, validation, validation_new, "member-validation")

    compare = '''        if (base_id >= context->function->value_count ||
            !minic_type_equal(context->function->values[base_id].type, base->type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
'''
    compare_new = '''        if (base_id >= context->function->value_count ||
            !minic_type_equal(context->function->values[base_id].type, base_value_type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
'''
    text = replace_once(text, compare, compare_new, "member-value-compare")
    print("M94 member-base value type applied")
    return text


def main() -> int:
    text = PATH.read_text()
    text = apply_detail(text)
    text = apply_pointer_difference(text)
    text = apply_member_base_value_type(text)
    PATH.write_text(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
