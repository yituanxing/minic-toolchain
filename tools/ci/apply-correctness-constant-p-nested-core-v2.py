#!/usr/bin/env python3
"""Avoid frontend ICE false negatives in nested Core-local constant-p queries.

In the frontend, __builtin_constant_p(non-ICE) must be evaluated conservatively
as 0.  During Core lowering, however, we have proven local constant facts.  An
outer logical AND, conditional, or bitwise AND must NOT be pre-folded by the
frontend before the deferred constant-p child can use those facts.  Otherwise
the Linux min()/clamp() signedness check can incorrectly retain a compiletime
assert call.  For expressions containing a deferred query, only the Core-local
evaluator can prove the result; if it cannot, conservatively keep runtime code.
"""
from pathlib import Path

p = Path("src/core/core_lower.c")
s = p.read_text()
fn = ("static bool core_const_eval_integer_with_locals(const MinicCoreLowerContext *context,\n"\n      "                                                MinicExpressionId expression_id,\n"\n      "                                                MinicConstValue *value) {\n")
helper = r'''
/* V2_NESTED_CONSTANT_P: identify deferred constant queries without
 * executing an operand.  Bounded traversal, no AST changes or side effects. */
static bool core_expr_contains_deferred_constant_p(
    const MinicC0Program *program, MinicExpressionId id, unsigned depth) {
    const MinicExpression *expr;

    if (program == NULL || id == MINIC_EXPRESSION_INVALID || depth > 128U) {
        return false;
    }
    expr = minic_c0_program_expression(program, id);
    if (expr == NULL) {
        return false;
    }
    if (expr->kind == MINIC_EXPRESSION_BUILTIN_UNARY &&
        expr->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_CONSTANT_P) {
        return true;
    }
    switch (expr->kind) {
    case MINIC_EXPRESSION_CAST:
    case MINIC_EXPRESSION_BITCAST:
    case MINIC_EXPRESSION_CONVERSION:
    case MINIC_EXPRESSION_UNARY:
    case MINIC_EXPRESSION_LVALUE_READ:
        return core_expr_contains_deferred_constant_p(
            program, expr->value.unary.operand, depth + 1U);
    case MINIC_EXPRESSION_BINARY:
        return core_expr_contains_deferred_constant_p(
                   program, expr->value.binary.left, depth + 1U) ||
               core_expr_contains_deferred_constant_p(
                   program, expr->value.binary.right, depth + 1U);
    case MINIC_EXPRESSION_CONDITIONAL:
        return core_expr_contains_deferred_constant_p(
                   program, expr->value.conditional.condition, depth + 1U) ||
               core_expr_contains_deferred_constant_p(
                   program, expr->value.conditional.when_true, depth + 1U) ||
               core_expr_contains_deferred_constant_p(
                   program, expr->value.conditional.when_false, depth + 1U);
    default:
        return false;
    }
}

'''
if s.count(fn) != 1:
    raise SystemExit(f"nested constant-p evaluator anchor count={s.count(fn)}")
s = s.replace(fn, helper + fn, 1)

old = """    if (minic_const_eval_integer(
            context->body->program, context->target, expression_id, value)) {
        return true;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
"""
new = """    /* The frontend ICE evaluator treats a deferred constant-p query as
     * unknown->0.  On outer '&&', '?:' and bitwise masks this prematurely
     * makes the entire expression a constant even if the Core local-fact
     * analysis can prove constant-p==1.  Try the Core evaluator first.
     * When it cannot prove a result, do not substitute the frontend's
     * pessimistic answer for the nested query. */
    if (!core_expr_contains_deferred_constant_p(
            context->body->program, expression_id, 0U) &&
        minic_const_eval_integer(
            context->body->program, context->target, expression_id, value)) {
        return true;
    }
    expression = minic_c0_program_expression(context->body->program, expression_id);
"""
if s.count(old) != 1:
    raise SystemExit(f"nested constant-p frontend gate count={s.count(old)}")
p.write_text(s.replace(old, new, 1))
print("CORE_NESTED_CONSTANT_P_LOCAL_FACTS_V2=APPLIED")
