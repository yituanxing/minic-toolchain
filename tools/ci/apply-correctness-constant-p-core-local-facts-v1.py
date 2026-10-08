#!/usr/bin/env python3
"""Preserve optimizer-aware __builtin_constant_p in Core reachability.

ICE evaluation must return a frontend constant 0 for unknown local values;
but Core has additional proven straight-line local facts.  Evaluate the
deferred query against those facts before calling the frontend evaluator;
otherwise the ICE fallback eagerly returns 0 and the existing Core-local
constant-p branch is unreachable.  This matters for Linux min()/clamp()
signedness guards with __auto_type temporary bounds.
"""
from pathlib import Path

p = Path("src/core/core_lower.c")
s = p.read_text()
old = """    if (minic_const_eval_integer(
            context->body->program, context->target, expression_id, value)) {
        return true;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
"""
new = """    /* The frontend ICE contract conservatively folds an unknown
     * __builtin_constant_p to 0.  But this Core evaluator is also allowed to
     * use sound, unescaped straight-line local facts; check those first.
     * No runtime evaluation or side effect of the operand occurs. */
    expression = minic_c0_program_expression(context->body->program, expression_id);
    if (expression != NULL &&
        expression->kind == MINIC_EXPRESSION_BUILTIN_UNARY &&
        expression->value.builtin_unary.operator_kind ==
            MINIC_BUILTIN_UNARY_CONSTANT_P) {
        MinicConstValue ignored_operand;
        bool known_constant;

        known_constant = core_const_eval_integer_with_locals(
            context, expression->value.builtin_unary.operand, &ignored_operand);
        value->type = minic_type_int();
        value->bits = known_constant ? 1U : 0U;
        return true;
    }
    if (minic_const_eval_integer(
            context->body->program, context->target, expression_id, value)) {
        return true;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
"""
n = s.count(old)
if n != 1:
    raise SystemExit(f"constant-p Core local-facts anchor count={n}")
p.write_text(s.replace(old, new, 1))
print("CORE_BUILTIN_CONSTANT_P_LOCAL_FACT_PRIORITY_V1=APPLIED")
