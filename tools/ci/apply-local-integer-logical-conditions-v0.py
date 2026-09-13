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
        result = expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_AND
                     ? !right_is_zero
                     : !right_is_zero;
        value->type = expression->type;
        value->bits = result ? 1U : 0U;
        return true;
    }
'''

count = text.count(anchor)
if count != 1:
    raise SystemExit(f"expected one local-aware comparison anchor, found {count}")
p.write_text(text.replace(anchor, logical + anchor, 1))
print("MINIC_LOCAL_INTEGER_LOGICAL_CONDITIONS_V0=APPLIED")
