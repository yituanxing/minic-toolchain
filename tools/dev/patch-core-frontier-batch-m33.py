#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"M33 {label}: expected one anchor, found {count}")
    p.write_text(text.replace(old, new, 1))


helper_anchor = '''static MinicCoreLowerStatus lower_direct_call(MinicCoreLowerContext *context,
'''
helper = r'''static bool core_integer_compound_instruction_kind(MinicBinaryOperator operator_kind,
                                                   MinicCoreInstructionKind *instruction_kind) {
    if (instruction_kind == NULL) {
        return false;
    }
    switch (operator_kind) {
    case MINIC_BINARY_ADD:
        *instruction_kind = MINIC_CORE_INSTRUCTION_INTEGER_ADD;
        return true;
    case MINIC_BINARY_SUBTRACT:
        *instruction_kind = MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT;
        return true;
    case MINIC_BINARY_MULTIPLY:
        *instruction_kind = MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY;
        return true;
    case MINIC_BINARY_DIVIDE:
        *instruction_kind = MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE;
        return true;
    case MINIC_BINARY_REMAINDER:
        *instruction_kind = MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER;
        return true;
    case MINIC_BINARY_BITWISE_AND:
        *instruction_kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND;
        return true;
    case MINIC_BINARY_BITWISE_XOR:
        *instruction_kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR;
        return true;
    case MINIC_BINARY_BITWISE_OR:
        *instruction_kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR;
        return true;
    case MINIC_BINARY_SHIFT_LEFT:
        *instruction_kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT;
        return true;
    case MINIC_BINARY_SHIFT_RIGHT:
        *instruction_kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT;
        return true;
    default:
        return false;
    }
}

static MinicCoreLowerStatus
lower_integer_compound_assignment(MinicCoreLowerContext *context,
                                  const MinicExpression *expression,
                                  MinicCoreValueId *value_id) {
    const MinicExpression *left_expression;
    const MinicExpression *right_expression;
    MinicCoreInstruction instruction;
    MinicCoreInstructionKind instruction_kind;
    MinicCoreObjectId address_object;
    MinicCoreObjectId left_object;
    MinicCoreValueId address;
    MinicCoreValueId left_source;
    MinicCoreValueId left_value;
    MinicCoreValueId result_value;
    MinicCoreValueId right_source;
    MinicCoreValueId right_value;
    MinicCoreLowerStatus status;
    MinicType address_type;
    MinicType left_operation_type;
    MinicType right_operation_type;
    MinicType target_type;
    bool is_shift;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || context->target == NULL || expression == NULL ||
        value_id == NULL || expression->kind != MINIC_EXPRESSION_COMPOUND_ASSIGNMENT) {
        return MINIC_CORE_LOWER_ERROR;
    }
    left_expression =
        minic_c0_program_expression(context->body->program, expression->value.binary.left);
    right_expression =
        minic_c0_program_expression(context->body->program, expression->value.binary.right);
    if (left_expression == NULL || right_expression == NULL ||
        left_expression->value_category != MINIC_VALUE_LVALUE ||
        !minic_type_is_integer(left_expression->type) ||
        !minic_type_is_integer(right_expression->type) ||
        !minic_type_equal(expression->type, left_expression->type) ||
        minic_type_is_const(left_expression->type) || minic_type_is_volatile(left_expression->type) ||
        !minic_type_unqualified(left_expression->type, &target_type) ||
        !minic_type_equal(target_type, left_expression->type) ||
        !core_integer_compound_instruction_kind(expression->value.binary.operator_kind,
                                                &instruction_kind) ||
        !minic_type_pointer_to(target_type, &address_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }

    is_shift = expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_LEFT ||
               expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_RIGHT;
    if (is_shift) {
        if (!minic_target_info_integer_promotion_for_program(context->target,
                                                             context->body->program,
                                                             target_type,
                                                             &left_operation_type) ||
            !minic_target_info_integer_promotion_for_program(context->target,
                                                             context->body->program,
                                                             right_expression->type,
                                                             &right_operation_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
    } else {
        if (!minic_target_info_integer_common_for_program(context->target,
                                                          context->body->program,
                                                          target_type,
                                                          right_expression->type,
                                                          &left_operation_type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        right_operation_type = left_operation_type;
    }

    /* Evaluate the modifiable lvalue exactly once.  Preserve both its address
     * and old value across arbitrary RHS control flow through the existing
     * MEMORY-form spill/reload seam. */
    status = lower_address(context, expression->value.binary.left, &address);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (address >= context->function->value_count ||
        !minic_type_equal(context->function->values[address].type, address_type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    status = spill_scalar_value(context,
                                left_expression->span,
                                address_type,
                                address,
                                &address_object);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_LOAD;
    instruction.span = left_expression->span;
    instruction.type = target_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.load.address = address;
    instruction.value.load.is_volatile = false;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &instruction, &left_source)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    status = append_integer_conversion(context,
                                       left_expression->span,
                                       left_operation_type,
                                       left_source,
                                       &left_value);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = spill_scalar_value(context,
                                left_expression->span,
                                left_operation_type,
                                left_value,
                                &left_object);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    status = lower_expression(context, expression->value.binary.right, &right_source);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = append_integer_conversion(context,
                                       right_expression->span,
                                       right_operation_type,
                                       right_source,
                                       &right_value);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = reload_scalar_value(context,
                                 left_expression->span,
                                 left_operation_type,
                                 left_object,
                                 &left_value);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = instruction_kind;
    instruction.span = expression->span;
    instruction.type = left_operation_type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.binary.left = left_value;
    instruction.value.binary.right = right_value;
    if (!minic_core_function_append_value_instruction(
            context->function, context->block_id, &instruction, &result_value)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    status = append_integer_conversion(
        context, expression->span, target_type, result_value, &result_value);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = reload_scalar_value(
        context, left_expression->span, address_type, address_object, &address);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
    instruction.span = expression->span;
    instruction.type = minic_type_void();
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.store.address = address;
    instruction.value.store.stored_value = result_value;
    instruction.value.store.is_volatile = false;
    if (!minic_core_function_append_effect_instruction(
            context->function, context->block_id, &instruction)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    *value_id = result_value;
    return MINIC_CORE_LOWER_OK;
}

'''
replace_once(
    "src/core/core_lower.c",
    helper_anchor,
    helper + helper_anchor,
    "compound helper insertion",
)

expression_anchor = '''    if (expression->kind == MINIC_EXPRESSION_ASSIGNMENT) {
'''
expression_replacement = '''    if (expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT) {
        return lower_integer_compound_assignment(context, expression, value_id);
    }
''' + expression_anchor
replace_once(
    "src/core/core_lower.c",
    expression_anchor,
    expression_replacement,
    "compound expression dispatch",
)

print("M33_PATCH_APPLIED")
