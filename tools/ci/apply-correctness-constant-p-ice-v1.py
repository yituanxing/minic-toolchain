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
anchor="""    case MINIC_EXPRESSION_BUILTIN_UNARY:
        return eval_builtin_unary(program, target, expression, depth, value);"""
replace="""    case MINIC_EXPRESSION_BUILTIN_UNARY: {
        if (expression->value.builtin_unary.operator_kind ==
            MINIC_BUILTIN_UNARY_CONSTANT_P) {
            MinicConstValue ignored_operand;
            bool known_constant;
            /* In an ICE, GCC permits __builtin_constant_p(dynamic_x) with
             * constant result 0.  Elsewhere Core may still use local facts
             * to prove the operand constant, so do not erase the AST query. */
            known_constant = eval_expression(
                program, target, expression->value.builtin_unary.operand,
                depth + 1U, &ignored_operand);
            value->type = minic_type_int();
            value->bits = known_constant ? 1U : 0U;
            return true;
        }
        return eval_builtin_unary(program, target, expression, depth, value);
    }"""
n=s.count(anchor)
if n!=1:raise SystemExit(f"constant_p ICE anchor count={n}")
p.write_text(s.replace(anchor,replace,1))
print("CORE_BUILTIN_CONSTANT_P_FRONTEND_ICE=APPLIED")
