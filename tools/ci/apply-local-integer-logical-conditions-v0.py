#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower.c")
text = p.read_text()

anchor = '''    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_EQUAL ||
         expression->value.binary.operator_kind == MINIC_BINARY_NOT_EQUAL ||
         expression->value.binary.operator_kind == MINIC_BINARY_LESS ||
         expression->value.binary.operator_kind == MINIC_BINARY_LESS_EQUAL ||
         expression->value.binary.operator_kind == MINIC_BINARY_GREATER ||
         expression->value.binary.operator_kind == MINIC_BINARY_GREATER_EQUAL)) {
'''
logical = '''    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_AND ||
         expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_OR)) {
        MinicConstValue left_value;
        MinicConstValue right_value;
        bool left_is_zero;
        bool right_is_zero;
        bool result;

        if (!core_const_eval_integer_with_locals(
                context, expression->value.binary.left, &left_value) ||
            !minic_const_value_is_zero(context->body->program,
                                       context->target,
                                       &left_value,
                                       &left_is_zero)) {
            return false;
        }
        if (expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_AND &&
            left_is_zero) {
            value->type = expression->type;
            value->bits = 0U;
            return true;
        }
        if (expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_OR &&
            !left_is_zero) {
            value->type = expression->type;
            value->bits = 1U;
            return true;
        }
        if (!core_const_eval_integer_with_locals(
                context, expression->value.binary.right, &right_value) ||
            !minic_const_value_is_zero(context->body->program,
                                       context->target,
                                       &right_value,
                                       &right_is_zero)) {
            return false;
        }
        result = !right_is_zero;
        value->type = expression->type;
        value->bits = result ? 1U : 0U;
        return true;
    }

    /* Linux min/max/clamp signedness masks combine optimizer-known local facts
       through ?:, integer addition and bitwise AND.  Compose only these narrow
       integer forms; the fact lifetime/escape rules remain unchanged. */
    if (expression->kind == MINIC_EXPRESSION_CONDITIONAL) {
        MinicConstValue condition_value;
        MinicConstValue selected_value;
        bool condition_is_zero;
        MinicExpressionId selected_id;

        if (!core_const_eval_integer_with_locals(
                context, expression->value.conditional.condition, &condition_value) ||
            !minic_const_value_is_zero(context->body->program,
                                       context->target,
                                       &condition_value,
                                       &condition_is_zero)) {
            return false;
        }
        selected_id = condition_is_zero ? expression->value.conditional.when_false
                                        : expression->value.conditional.when_true;
        if (!core_const_eval_integer_with_locals(context, selected_id, &selected_value)) {
            return false;
        }
        return minic_const_value_convert_integer(context->body->program,
                                                 context->target,
                                                 &selected_value,
                                                 expression->type,
                                                 value);
    }
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_ADD ||
         expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_AND)) {
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;
        MinicConstValue left;
        MinicConstValue right;
        MinicConstValue left_common;
        MinicConstValue right_common;
        MinicConstValue result_common;
        MinicType common_type;

        left_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.left);
        right_expression = minic_c0_program_expression(
            context->body->program, expression->value.binary.right);
        if (left_expression == NULL || right_expression == NULL ||
            !minic_type_is_integer(left_expression->type) ||
            !minic_type_is_integer(right_expression->type) ||
            !core_const_eval_integer_with_locals(
                context, expression->value.binary.left, &left) ||
            !core_const_eval_integer_with_locals(
                context, expression->value.binary.right, &right) ||
            !minic_target_info_integer_common_for_program(context->target,
                                                          context->body->program,
                                                          left_expression->type,
                                                          right_expression->type,
                                                          &common_type) ||
            !minic_const_value_convert_integer(context->body->program,
                                               context->target,
                                               &left,
                                               common_type,
                                               &left_common) ||
            !minic_const_value_convert_integer(context->body->program,
                                               context->target,
                                               &right,
                                               common_type,
                                               &right_common)) {
            return false;
        }
        result_common.type = common_type;
        result_common.bits = expression->value.binary.operator_kind == MINIC_BINARY_ADD
                                 ? left_common.bits + right_common.bits
                                 : left_common.bits & right_common.bits;
        return minic_const_value_convert_integer(context->body->program,
                                                 context->target,
                                                 &result_common,
                                                 expression->type,
                                                 value);
    }
'''

count = text.count(anchor)
if count != 1:
    raise SystemExit(f"expected one local-aware comparison anchor, found {count}")
p.write_text(text.replace(anchor, logical + anchor, 1))
print("MINIC_LOCAL_INTEGER_LOGICAL_CONDITIONS_V0=APPLIED")
