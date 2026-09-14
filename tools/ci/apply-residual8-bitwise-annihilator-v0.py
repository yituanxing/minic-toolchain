#!/usr/bin/env python3
from pathlib import Path

p = Path("src/core/core_lower.c")
text = p.read_text()
marker = "RESIDUAL8_BITWISE_ZERO_ANNIHILATOR_V0"
if marker in text:
    print("MINIC_RESIDUAL8_BITWISE_ZERO_ANNIHILATOR_V0=ALREADY")
    raise SystemExit(0)

fn = "static bool core_const_eval_integer_with_locals(const MinicCoreLowerContext *context,\n"
start = text.find(fn)
if start < 0:
    raise SystemExit("local integer evaluator missing")
anchor = '''    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        (expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_AND ||
         expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_OR)) {
'''
pos = text.find(anchor, start)
if pos < 0:
    raise SystemExit("logical evaluator anchor missing")
block = r'''    /* RESIDUAL8_BITWISE_ZERO_ANNIHILATOR_V0: x & 0 and 0 & x are
       constant zero when evaluating the unknown side has no side effects.
       This closes CONFIG-disabled predicates such as inode->i_flags & 0. */
    if (expression->kind == MINIC_EXPRESSION_BINARY &&
        expression->value.binary.operator_kind == MINIC_BINARY_BITWISE_AND) {
        MinicConstValue annihilator_operand;
        bool annihilator_is_zero;

        if (core_const_eval_integer_with_locals(
                context, expression->value.binary.left, &annihilator_operand) &&
            minic_const_value_is_zero(context->body->program,
                                      context->target,
                                      &annihilator_operand,
                                      &annihilator_is_zero) &&
            annihilator_is_zero &&
            core_cfg_pure_call_argument(
                context, expression->value.binary.right, 0U)) {
            value->type = expression->type;
            value->bits = 0U;
            return true;
        }
        if (core_const_eval_integer_with_locals(
                context, expression->value.binary.right, &annihilator_operand) &&
            minic_const_value_is_zero(context->body->program,
                                      context->target,
                                      &annihilator_operand,
                                      &annihilator_is_zero) &&
            annihilator_is_zero &&
            core_cfg_pure_call_argument(
                context, expression->value.binary.left, 0U)) {
            value->type = expression->type;
            value->bits = 0U;
            return true;
        }
    }

'''
text = text[:pos] + block + text[pos:]
p.write_text(text)
print("MINIC_RESIDUAL8_BITWISE_ZERO_ANNIHILATOR_V0=APPLIED")
