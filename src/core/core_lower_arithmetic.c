#include "core/core_lower_internal.h"
#include "core/local_facts.h"
#include "frontend/const_eval.h"
#include "frontend/expression_semantics.h"
#include <string.h>

/* Ordered source-to-Core scalar arithmetic operations. These probes own
 * arithmetic evaluation, promotion and spill/reload semantics, while the
 * generic expression driver owns dispatch order and the final const fold.
 * A probe is handled only if its original path returned a status. */

MinicCoreLowerStatus minic_core_lower_try_arithmetic_unary(
    MinicCoreLowerContext *context, const MinicExpression *expression,
    MinicCoreValueId *value_id, bool *handled) {
    MinicCoreInstruction instruction;
    if (context == NULL || expression == NULL || value_id == NULL || handled == NULL)
        return MINIC_CORE_LOWER_ERROR;
    *handled = false;
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.span = expression->span;
    instruction.type = expression->type;
    instruction.result = MINIC_CORE_VALUE_INVALID;

    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        expression->value.unary.operator_kind == MINIC_UNARY_PLUS &&
        (minic_type_is_float(expression->type) || minic_type_is_double(expression->type) ||
         minic_type_is_long_double(expression->type))) {
        *handled = true;
        MinicCoreLowerStatus status;
        MinicCoreValueId operand_value;

        status = lower_expression(context, expression->value.unary.operand, &operand_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (operand_value >= context->function->value_count ||
            !minic_type_equal(context->function->values[operand_value].type, expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        *value_id = operand_value;
        return MINIC_CORE_LOWER_OK;
    }
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        expression->value.unary.operator_kind == MINIC_UNARY_NEGATE &&
        minic_type_is_long_double(expression->type)) {
        *handled = true;
        MinicCoreInstruction zero_instruction;
        MinicCoreValueId operand_value;
        MinicCoreValueId zero_value;
        MinicCoreLowerStatus status;

        status = minic_core_lower_internal_lower_scalar_assignment_value(context, minic_type_long_double(),
                                               expression->value.unary.operand, &operand_value);
        if (status != MINIC_CORE_LOWER_OK) return status;
        (void)memset(&zero_instruction, 0, sizeof(zero_instruction));
        /* FIXME(MC-051): 0L - x is not the semantic equivalent of unary
         * -x for signed zero: nearest-mode +0L - +0L gives +0L, but GNU C
         * unary negation must produce -0L. Preserve the sign operation. */
        zero_instruction.kind = MINIC_CORE_INSTRUCTION_FLOATING_CONSTANT;
        zero_instruction.span = expression->span;
        zero_instruction.type = minic_type_long_double();
        zero_instruction.result = MINIC_CORE_VALUE_INVALID;
        zero_instruction.value.floating128_bits.low = 0U;
        zero_instruction.value.floating128_bits.high = 0U;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &zero_instruction, &zero_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        return minic_core_lower_internal_append_long_double_binary_helper(
            context, expression->span, "__subtf3", zero_value, operand_value,
            minic_type_long_double(), value_id);
    }
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        expression->value.unary.operator_kind == MINIC_UNARY_NEGATE &&
        minic_type_is_float(expression->type)) {
        *handled = true;
        const MinicExpression *operand_expression;
        MinicCoreInstruction float_instruction;
        MinicCoreLowerStatus status;
        MinicCoreValueId operand_value;
        MinicCoreValueId widened_value;
        MinicCoreValueId negated_value;

        operand_expression = minic_c0_program_expression(
            context->body->program, expression->value.unary.operand);
        if (operand_expression == NULL || !minic_type_is_float(operand_expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_expression(context, expression->value.unary.operand, &operand_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (operand_value >= context->function->value_count ||
            !minic_type_is_float(context->function->values[operand_value].type)) {
            return MINIC_CORE_LOWER_ERROR;
        }

        (void)memset(&float_instruction, 0, sizeof(instruction));
        float_instruction.kind = MINIC_CORE_INSTRUCTION_FLOAT_TO_DOUBLE;
        float_instruction.span = expression->span;
        float_instruction.type = minic_type_double();
        float_instruction.result = MINIC_CORE_VALUE_INVALID;
        float_instruction.value.operand = operand_value;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &float_instruction, &widened_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }

        float_instruction.kind = MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE;
        float_instruction.type = minic_type_double();
        float_instruction.value.operand = widened_value;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &float_instruction, &negated_value)) {
            return MINIC_CORE_LOWER_ERROR;
        }

        float_instruction.kind = MINIC_CORE_INSTRUCTION_DOUBLE_TO_FLOAT;
        float_instruction.type = expression->type;
        float_instruction.value.operand = negated_value;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &float_instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        expression->value.unary.operator_kind == MINIC_UNARY_NEGATE &&
        minic_type_is_double(expression->type)) {
        *handled = true;
        const MinicExpression *operand_expression;
        MinicCoreValueId operand_value;
        MinicCoreLowerStatus status;

        operand_expression = minic_c0_program_expression(
            context->body->program, expression->value.unary.operand);
        if (operand_expression == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = minic_core_lower_internal_lower_scalar_assignment_value(context,
                                               minic_type_double(),
                                               expression->value.unary.operand,
                                               &operand_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_DOUBLE_NEGATE;
        instruction.span = expression->span;
        instruction.type = minic_type_double();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.operand = operand_value;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        expression->value.unary.operator_kind == MINIC_UNARY_NEGATE) {
        *handled = true;
        const MinicExpression *operand_expression;
        MinicCoreValueId operand_value;
        MinicCoreLowerStatus status;
        MinicType promoted_type;

        operand_expression = minic_c0_program_expression(
            context->body->program, expression->value.unary.operand);
        if (context->target == NULL || operand_expression == NULL ||
            !minic_type_is_integer(expression->type) ||
            !minic_type_is_integer(operand_expression->type) ||
            !minic_target_info_integer_promotion_for_program(
                context->target, context->body->program, operand_expression->type, &promoted_type) ||
            !minic_type_equal(promoted_type, expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_expression(context, expression->value.unary.operand, &operand_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = minic_core_lower_internal_append_integer_conversion(
            context, operand_expression->span, promoted_type, operand_value, &operand_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_NEGATE;
        instruction.span = expression->span;
        instruction.type = expression->type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.operand = operand_value;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        expression->value.unary.operator_kind == MINIC_UNARY_BITWISE_NOT) {
        *handled = true;
        const MinicExpression *operand_expression;
        MinicCoreValueId operand_value;
        MinicCoreLowerStatus status;
        MinicType promoted_type;

        operand_expression = minic_c0_program_expression(
            context->body->program, expression->value.unary.operand);
        if (context->target == NULL || operand_expression == NULL ||
            !minic_type_is_integer(expression->type) ||
            !minic_type_is_integer(operand_expression->type) ||
            !minic_target_info_integer_promotion_for_program(
                context->target, context->body->program, operand_expression->type, &promoted_type) ||
            !minic_type_equal(promoted_type, expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_expression(context, expression->value.unary.operand, &operand_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = minic_core_lower_internal_append_integer_conversion(
            context, operand_expression->span, promoted_type, operand_value, &operand_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_NOT;
        instruction.span = expression->span;
        instruction.type = expression->type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.operand = operand_value;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        expression->value.unary.operator_kind == MINIC_UNARY_LOGICAL_NOT) {
        *handled = true;
        MinicCoreValueId operand_value;
        MinicCoreLowerStatus status;

        if (!minic_type_equal(expression->type, minic_type_int())) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = lower_expression(context, expression->value.unary.operand, &operand_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (operand_value >= context->function->value_count ||
            !core_memory_scalar_type(context->function->values[operand_value].type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_IS_ZERO;
        instruction.span = expression->span;
        instruction.type = expression->type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.operand = operand_value;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    *handled = false;
    return MINIC_CORE_LOWER_OK;
}

MinicCoreLowerStatus minic_core_lower_try_arithmetic_binary(
    MinicCoreLowerContext *context, const MinicExpression *expression,
    MinicCoreValueId *value_id, bool *handled) {
    MinicCoreInstruction instruction;
    if (context == NULL || expression == NULL || value_id == NULL || handled == NULL)
        return MINIC_CORE_LOWER_ERROR;
    *handled = false;
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.span = expression->span;
    instruction.type = expression->type;
    instruction.result = MINIC_CORE_VALUE_INVALID;

    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT ||
         expression->value.binary.operator_kind == MINIC_BINARY_MULTIPLY ||
         expression->value.binary.operator_kind == MINIC_BINARY_DIVIDE ||
         expression->value.binary.operator_kind == MINIC_BINARY_REMAINDER ||
         expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_XOR) &&
        minic_type_is_integer(expression->type)) {
        *handled = true;
        MinicCoreValueId left;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;
        status = minic_core_lower_internal_lower_integer_binary_operands(context,
                                               expression->value.binary.left,
                                               expression->value.binary.right,
                                               expression->type,
                                               &left,
                                               &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        switch (expression->value.binary.operator_kind) {
        case MINIC_BINARY_SUBTRACT:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT;
            break;
        case MINIC_BINARY_MULTIPLY:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY;
            break;
        case MINIC_BINARY_DIVIDE:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE;
            break;
        case MINIC_BINARY_REMAINDER:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER;
            break;
        case MINIC_BINARY_BITWISE_XOR:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR;
            break;
        default:
            return MINIC_CORE_LOWER_ERROR;
        }
        instruction.value.binary.left = left;
        instruction.value.binary.right = right;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT ||
         expression->value.binary.operator_kind == MINIC_BINARY_MULTIPLY ||
         expression->value.binary.operator_kind == MINIC_BINARY_DIVIDE) &&
        minic_type_is_long_double(expression->type)) {
        *handled = true;
        const char *helper;
        MinicCoreValueId left;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;

        status = minic_core_lower_internal_lower_floating_binary_operands(
            context, expression->value.binary.left, expression->value.binary.right,
            minic_type_long_double(), &left, &right);
        if (status != MINIC_CORE_LOWER_OK) return status;
        helper = expression->value.binary.operator_kind == MINIC_BINARY_ADD ? "__addtf3" :
                 expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT ? "__subtf3" :
                 expression->value.binary.operator_kind == MINIC_BINARY_MULTIPLY ? "__multf3" :
                 "__divtf3";
        return minic_core_lower_internal_append_long_double_binary_helper(
            context, expression->span, helper, left, right, minic_type_long_double(), value_id);
    }

    /* RUNTIME_R0_SCALAR_FLOATING: preserve left-to-right evaluation by
       spilling the left binary32/binary64 value across RHS lowering. */
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT ||
         expression->value.binary.operator_kind == MINIC_BINARY_MULTIPLY ||
         expression->value.binary.operator_kind == MINIC_BINARY_DIVIDE) &&
        (minic_type_is_float(expression->type) || minic_type_is_double(expression->type))) {
        *handled = true;
        MinicCoreValueId left;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;
        bool is_float;

        is_float = minic_type_is_float(expression->type);
        status = minic_core_lower_internal_lower_floating_binary_operands(context,
                                              expression->value.binary.left,
                                              expression->value.binary.right,
                                              expression->type,
                                              &left,
                                              &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        switch (expression->value.binary.operator_kind) {
        case MINIC_BINARY_ADD:
            instruction.kind = is_float ? MINIC_CORE_INSTRUCTION_FLOAT_ADD
                                        : MINIC_CORE_INSTRUCTION_DOUBLE_ADD;
            break;
        case MINIC_BINARY_SUBTRACT:
            instruction.kind = is_float ? MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT
                                        : MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT;
            break;
        case MINIC_BINARY_MULTIPLY:
            instruction.kind = is_float ? MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY
                                        : MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY;
            break;
        case MINIC_BINARY_DIVIDE:
            instruction.kind = is_float ? MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE
                                        : MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE;
            break;
        default:
            return MINIC_CORE_LOWER_ERROR;
        }
        instruction.span = expression->span;
        instruction.type = expression->type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = left;
        instruction.value.binary.right = right;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }

    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_ADD) {
        *handled = true;
        MinicCoreValueId left;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;

        if (!minic_type_is_integer(expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = minic_core_lower_internal_lower_integer_binary_operands(context,
                                               expression->value.binary.left,
                                               expression->value.binary.right,
                                               expression->type,
                                               &left,
                                               &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_ADD;
        instruction.value.binary.left = left;
        instruction.value.binary.right = right;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_AND) {
        *handled = true;
        MinicCoreValueId left;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;

        if (!minic_type_is_integer(expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = minic_core_lower_internal_lower_integer_binary_operands(context,
                                               expression->value.binary.left,
                                               expression->value.binary.right,
                                               expression->type,
                                               &left,
                                               &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND;
        instruction.value.binary.left = left;
        instruction.value.binary.right = right;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_OR) {
        *handled = true;
        MinicCoreValueId left;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;

        if (!minic_type_is_integer(expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = minic_core_lower_internal_lower_integer_binary_operands(context,
                                               expression->value.binary.left,
                                               expression->value.binary.right,
                                               expression->type,
                                               &left,
                                               &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR;
        instruction.value.binary.left = left;
        instruction.value.binary.right = right;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_LEFT ||
         expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_RIGHT)) {
        *handled = true;
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;
        MinicCoreObjectId left_object;
        MinicCoreValueId left;
        MinicCoreValueId left_source;
        MinicCoreValueId right;
        MinicCoreLowerStatus status;

        if (!minic_type_is_integer(expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        left_expression =
            minic_c0_program_expression(context->body->program, expression->value.binary.left);
        right_expression =
            minic_c0_program_expression(context->body->program, expression->value.binary.right);
        if (left_expression == NULL || right_expression == NULL ||
            !minic_type_is_integer(left_expression->type) ||
            !minic_type_is_integer(right_expression->type)) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        status = lower_expression(context, expression->value.binary.left, &left_source);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = minic_core_lower_internal_append_integer_conversion(
            context, left_expression->span, expression->type, left_source, &left);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = minic_core_lower_internal_spill_scalar_value(
            context, left_expression->span, expression->type, left, &left_object);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = lower_expression(context, expression->value.binary.right, &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = minic_core_lower_internal_reload_scalar_value(
            context, left_expression->span, expression->type, left_object, &left);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (left >= context->function->value_count || right >= context->function->value_count ||
            !minic_type_equal(context->function->values[left].type, expression->type) ||
            !minic_type_is_integer(context->function->values[right].type)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        instruction.kind = expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_LEFT
                               ? MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT
                               : MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT;
        instruction.value.binary.left = left;
        instruction.value.binary.right = right;
        return minic_core_function_append_value_instruction(
                   context->function, context->block_id, &instruction, value_id)
                   ? MINIC_CORE_LOWER_OK
                   : MINIC_CORE_LOWER_ERROR;
    }
    *handled = false;
    return MINIC_CORE_LOWER_OK;
}

MinicCoreLowerStatus minic_core_lower_try_arithmetic_compound(
    MinicCoreLowerContext *context, const MinicExpression *expression,
    MinicCoreValueId *value_id, bool *handled) {
    MinicCoreInstruction instruction;
    if (context == NULL || expression == NULL || value_id == NULL || handled == NULL)
        return MINIC_CORE_LOWER_ERROR;
    *handled = false;
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.span = expression->span;
    instruction.type = expression->type;
    instruction.result = MINIC_CORE_VALUE_INVALID;

    /* RUNTIME_R1_DOUBLE_COMPOUND_ASSIGNMENT: binary64 compound arithmetic
       reuses the same evaluate-address/load/spill/RHS/reload/store ownership
       as integer compound assignment.  Keep remainder/bitwise/shift excluded:
       C does not define those operators for floating operands. */
    if (expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT ||
         expression->value.binary.operator_kind == MINIC_BINARY_MULTIPLY ||
         expression->value.binary.operator_kind == MINIC_BINARY_DIVIDE)) {
        *handled = true;
        const MinicExpression *source;
        const MinicExpression *target;
        MinicCoreObjectId address_object;
        MinicCoreObjectId current_object;
        MinicCoreValueId address;
        MinicCoreValueId current;
        MinicCoreValueId right;
        MinicCoreValueId result;
        MinicCoreLowerStatus status;
        MinicType address_type;
        MinicType expression_value_type;
        MinicType stored_type;

        target = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        source = minic_c0_program_expression(
            context->body->program, expression->value.binary.right);
        if (target != NULL && source != NULL &&
            target->value_category == MINIC_VALUE_LVALUE &&
            !minic_type_is_const(target->type) &&
            minic_type_unqualified(target->type, &stored_type) &&
            (minic_type_is_float(stored_type) || minic_type_is_double(stored_type) ||
             minic_type_is_long_double(stored_type))) {
            if (!minic_type_unqualified(expression->type, &expression_value_type) ||
                !minic_type_equal(expression_value_type, stored_type)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            status = lower_address(context, expression->value.binary.left, &address);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_LOAD;
            instruction.span = target->span;
            instruction.type = stored_type;
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.load.address = address;
            instruction.value.load.is_volatile = minic_type_is_volatile(target->type);
            if (!minic_core_function_append_value_instruction(
                    context->function, context->block_id, &instruction, &current) ||
                address >= context->function->value_count) {
                return MINIC_CORE_LOWER_ERROR;
            }
            address_type = context->function->values[address].type;
            status = minic_core_lower_internal_spill_scalar_value(
                context, target->span, address_type, address, &address_object);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            status = minic_core_lower_internal_spill_scalar_value(
                context, target->span, stored_type, current, &current_object);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            status = minic_core_lower_internal_lower_scalar_assignment_value(
                context, stored_type, expression->value.binary.right, &right);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            status = minic_core_lower_internal_reload_scalar_value(
                context, target->span, stored_type, current_object, &current);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            status = minic_core_lower_internal_reload_scalar_value(
                context, target->span, address_type, address_object, &address);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            if (minic_type_is_long_double(stored_type)) {
                const char *helper =
                    expression->value.binary.operator_kind == MINIC_BINARY_ADD ? "__addtf3" :
                    expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT ? "__subtf3" :
                    expression->value.binary.operator_kind == MINIC_BINARY_MULTIPLY ? "__multf3" :
                    "__divtf3";
                status = minic_core_lower_internal_append_long_double_binary_helper(
                    context, expression->span, helper, current, right,
                    minic_type_long_double(), &result);
                if (status != MINIC_CORE_LOWER_OK) return status;
            } else {
                (void)memset(&instruction, 0, sizeof(instruction));
                switch (expression->value.binary.operator_kind) {
                case MINIC_BINARY_ADD:
                    instruction.kind = minic_type_is_float(stored_type)
                                           ? MINIC_CORE_INSTRUCTION_FLOAT_ADD
                                           : MINIC_CORE_INSTRUCTION_DOUBLE_ADD;
                    break;
                case MINIC_BINARY_SUBTRACT:
                    instruction.kind = minic_type_is_float(stored_type)
                                           ? MINIC_CORE_INSTRUCTION_FLOAT_SUBTRACT
                                           : MINIC_CORE_INSTRUCTION_DOUBLE_SUBTRACT;
                    break;
                case MINIC_BINARY_MULTIPLY:
                    instruction.kind = minic_type_is_float(stored_type)
                                           ? MINIC_CORE_INSTRUCTION_FLOAT_MULTIPLY
                                           : MINIC_CORE_INSTRUCTION_DOUBLE_MULTIPLY;
                    break;
                case MINIC_BINARY_DIVIDE:
                    instruction.kind = minic_type_is_float(stored_type)
                                           ? MINIC_CORE_INSTRUCTION_FLOAT_DIVIDE
                                           : MINIC_CORE_INSTRUCTION_DOUBLE_DIVIDE;
                    break;
                default:
                    return MINIC_CORE_LOWER_ERROR;
                }
                instruction.span = expression->span;
                instruction.type = stored_type;
                instruction.result = MINIC_CORE_VALUE_INVALID;
                instruction.value.binary.left = current;
                instruction.value.binary.right = right;
                if (!minic_core_function_append_value_instruction(
                        context->function, context->block_id, &instruction, &result)) {
                    return MINIC_CORE_LOWER_ERROR;
                }
            }
            (void)memset(&instruction, 0, sizeof(instruction));
            instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
            instruction.span = expression->span;
            instruction.type = minic_type_void();
            instruction.result = MINIC_CORE_VALUE_INVALID;
            instruction.value.store.address = address;
            instruction.value.store.stored_value = result;
            instruction.value.store.is_volatile = minic_type_is_volatile(target->type);
            if (!minic_core_function_append_effect_instruction(
                    context->function, context->block_id, &instruction)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            *value_id = result;
            return MINIC_CORE_LOWER_OK;
        }
    }

    /* M51_SHIFT_COMPOUND_ASSIGNMENT: shifts use integer promotions on each operand
       independently; unlike arithmetic compound assignments they do not use the
       usual arithmetic conversions to a shared operand type. */
    /* BATCH_S_ARITHMETIC_COMPOUND_ASSIGNMENT: multiplication, division and
       remainder use the same usual-arithmetic-conversion path already owned by
       += and -=.  The Core arithmetic opcodes already exist; extend the generic
       load/operate/convert/store seam rather than special-casing qtree_depth. */
    if (expression->kind == MINIC_EXPRESSION_COMPOUND_ASSIGNMENT &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_SUBTRACT ||
         expression->value.binary.operator_kind == MINIC_BINARY_MULTIPLY ||
         expression->value.binary.operator_kind == MINIC_BINARY_DIVIDE ||
         expression->value.binary.operator_kind == MINIC_BINARY_REMAINDER ||
         expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_LEFT ||
         expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_RIGHT ||
         expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_AND ||
         expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_XOR ||
         expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_OR)) {
        *handled = true;
        const MinicExpression *source;
        const MinicExpression *target;
        MinicCoreObjectId address_object;
        MinicCoreObjectId current_object;
        MinicCoreValueId address;
        MinicCoreValueId current;
        MinicCoreValueId current_common;
        MinicCoreValueId right;
        MinicCoreValueId right_common;
        MinicCoreValueId result;
        MinicCoreValueId stored_value;
        MinicCoreLowerStatus status;
        MinicType address_type;
        MinicType common_type;
        MinicType right_type;
        MinicType stored_type;
        bool shift_assignment;

        target = minic_c0_program_expression(context->body->program, expression->value.binary.left);
        source =
            minic_c0_program_expression(context->body->program, expression->value.binary.right);
        if (target == NULL || source == NULL || target->value_category != MINIC_VALUE_LVALUE ||
            !minic_type_equal(expression->type, target->type) ||
            minic_type_is_const(target->type) ||
            !minic_type_unqualified(target->type, &stored_type) ||
            !minic_type_is_integer(stored_type) || !minic_type_is_integer(source->type) ||
            context->target == NULL) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        shift_assignment =
            expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_LEFT ||
            expression->value.binary.operator_kind == MINIC_BINARY_SHIFT_RIGHT;
        if (shift_assignment) {
            if (!minic_target_info_integer_promotion_for_program(
                    context->target, context->body->program, stored_type, &common_type) ||
                !minic_target_info_integer_promotion_for_program(
                    context->target, context->body->program, source->type, &right_type)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
        } else {
            if (!minic_target_info_integer_common_for_program(context->target,
                                                              context->body->program,
                                                              stored_type,
                                                              source->type,
                                                              &common_type)) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            right_type = common_type;
        }
        status = lower_address(context, expression->value.binary.left, &address);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_LOAD;
        instruction.span = target->span;
        instruction.type = stored_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.load.address = address;
        instruction.value.load.is_volatile = minic_type_is_volatile(target->type);
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &current)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status =
            minic_core_lower_internal_append_integer_conversion(context, target->span, common_type, current, &current_common);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (address >= context->function->value_count) {
            return MINIC_CORE_LOWER_ERROR;
        }
        address_type = context->function->values[address].type;
        status = minic_core_lower_internal_spill_scalar_value(context, target->span, address_type, address, &address_object);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status =
            minic_core_lower_internal_spill_scalar_value(context, target->span, common_type, current_common, &current_object);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = lower_expression(context, expression->value.binary.right, &right);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status =
            minic_core_lower_internal_append_integer_conversion(context, source->span, right_type, right, &right_common);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = minic_core_lower_internal_reload_scalar_value(
            context, target->span, common_type, current_object, &current_common);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = minic_core_lower_internal_reload_scalar_value(context, target->span, address_type, address_object, &address);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        switch (expression->value.binary.operator_kind) {
        case MINIC_BINARY_ADD:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_ADD;
            break;
        case MINIC_BINARY_SUBTRACT:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SUBTRACT;
            break;
        case MINIC_BINARY_MULTIPLY:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_MULTIPLY;
            break;
        case MINIC_BINARY_DIVIDE:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_DIVIDE;
            break;
        case MINIC_BINARY_REMAINDER:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_REMAINDER;
            break;
        case MINIC_BINARY_SHIFT_LEFT:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_LEFT;
            break;
        case MINIC_BINARY_SHIFT_RIGHT:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_SHIFT_RIGHT;
            break;
        case MINIC_BINARY_BITWISE_AND:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_AND;
            break;
        case MINIC_BINARY_BITWISE_XOR:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_XOR;
            break;
        case MINIC_BINARY_BITWISE_OR:
            instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_BITWISE_OR;
            break;
        default:
            return MINIC_CORE_LOWER_ERROR;
        }
        instruction.span = expression->span;
        instruction.type = common_type;
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = current_common;
        instruction.value.binary.right = right_common;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &result)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = minic_core_lower_internal_append_integer_conversion(
            context, expression->span, stored_type, result, &stored_value);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_STORE;
        instruction.span = expression->span;
        instruction.type = minic_type_void();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.store.address = address;
        instruction.value.store.stored_value = stored_value;
        instruction.value.store.is_volatile = minic_type_is_volatile(target->type);
        if (!minic_core_function_append_effect_instruction(
                context->function, context->block_id, &instruction)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_OR &&
            target->kind == MINIC_EXPRESSION_LOCAL) {
            MinicConstValue literal;
            if (minic_const_eval_integer(context->body->program,
                                         context->target,
                                         expression->value.binary.right,
                                         &literal)) {
                (void)minic_core_local_fact_or_constant(
                    context, target->value.local_id, &literal);
            }
        }
        *value_id = stored_value;
        return MINIC_CORE_LOWER_OK;
    }
    *handled = false;
    return MINIC_CORE_LOWER_OK;
}

