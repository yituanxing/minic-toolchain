#!/usr/bin/env python3
from pathlib import Path

p = Path("src/compiler/compiler.c")
text = p.read_text()
old = r'''    if (expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
        const MinicExpression *operand = minic_c0_program_expression(
            program, expression->value.unary.operand);
        return operand != NULL && operand->kind == MINIC_EXPRESSION_LOCAL && false;
    }
    if (expression->kind == MINIC_EXPRESSION_ADDRESS_OF) {
'''
new = r'''    if (expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
        const MinicExpression *operand = minic_c0_program_expression(
            program, expression->value.unary.operand);
        return operand != NULL && operand->kind == MINIC_EXPRESSION_LOCAL && false;
    }
    /* A nested constant array subscript is still a static lvalue rooted in the
     * same global object.  This matters for addresses such as
     * &global_matrix[CONST_A][CONST_B]: ADDRESS_OF sees the outer SUBSCRIPT,
     * then recursion must be able to walk the inner array-valued SUBSCRIPT. */
    if (expression->kind == MINIC_EXPRESSION_SUBSCRIPT &&
        minic_type_is_array(expression->type)) {
        MinicConstValue index_value;
        return minic_inline_symbolic_static_expression_depth(
                   program, target, expression->value.subscript.base, depth + 1U) &&
               minic_const_eval_integer(
                   program, target, expression->value.subscript.index, &index_value);
    }
    if (expression->kind == MINIC_EXPRESSION_ADDRESS_OF) {
'''
count = text.count(old)
if count != 1:
    raise SystemExit(f"expected one symbolic nested-array anchor, found {count}")
p.write_text(text.replace(old, new, 1))
print("MINIC_INLINE_SPECIALIZATION_SYMBOLIC_NESTED_ARRAY_V0=APPLIED")
