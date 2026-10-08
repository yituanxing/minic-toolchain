#!/usr/bin/env python3
"""Restore ICE evaluation of deferred __builtin_constant_p.

The Canonical local-facts patch retains unresolved builtin_constant_p as an
unevaluated AST unary query for Core lowering.  In an integer constant
expression (notably __builtin_choose_expr and _Static_assert) the same query
must evaluate to an integer constant: 1 when the operand is known constant,
otherwise conservatively 0.  This is the compile-time GCC contract; unlike
turning every query into literal 0, Core-local fact optimizations remain intact.
"""
from pathlib import Path
p=Path("src/frontend/const_eval.c")
s=p.read_text()
anchor="""    switch (expression->kind) {
    case MINIC_EXPRESSION_INTEGER:
        value->type = expression->type;"""
replace="""    switch (expression->kind) {
    case MINIC_EXPRESSION_BUILTIN_UNARY: {
        MinicConstValue ignored_operand;
        bool known_constant;
        if (expression->value.builtin_unary.operator_kind !=
            MINIC_BUILTIN_UNARY_CONSTANT_P) {
            return false;
        }
        /* GCC accepts __builtin_constant_p(variable) as an integer
         * constant expression with result 0. Preserve the unresolved
         * query for Core lowering elsewhere, but answer it here in ICE
         * contexts without ever evaluating its operand at runtime. */
        known_constant = eval_expression(
            program, target, expression->value.builtin_unary.operand,
            depth + 1U, &ignored_operand);
        value->type = minic_type_int();
        value->bits = known_constant ? 1U : 0U;
        return true;
    }
    case MINIC_EXPRESSION_INTEGER:
        value->type = expression->type;"""
n=s.count(anchor)
if n!=1:raise SystemExit(f"constant_p ICE anchor count={n}")
p.write_text(s.replace(anchor,replace,1))
print("CORE_BUILTIN_CONSTANT_P_FRONTEND_ICE=APPLIED")
