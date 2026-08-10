#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]

path = root / "src/frontend/ast_verifier.c"
text = path.read_text()
old = '''    case MINIC_EXPRESSION_BUILTIN_UNARY: {
        const MinicExpression *operand;

        operand = expression_before(
            program, expression->value.builtin_unary.operand, expression_index);
        return expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_CLZLL &&
               operand != NULL &&
               minic_type_equal(operand->type, minic_type_unsigned_long_long()) &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_equal(expression->type, minic_type_int());
    }
'''
new = '''    case MINIC_EXPRESSION_BUILTIN_UNARY: {
        const MinicExpression *builtin_operand;

        builtin_operand = expression_before(
            program, expression->value.builtin_unary.operand, expression_index);
        return expression->value.builtin_unary.operator_kind == MINIC_BUILTIN_UNARY_CLZLL &&
               builtin_operand != NULL &&
               minic_type_equal(builtin_operand->type, minic_type_unsigned_long_long()) &&
               expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_equal(expression->type, minic_type_int());
    }
'''
if text.count(old) != 1:
    raise SystemExit(f"verifier builtin unary anchor count={text.count(old)}")
path.write_text(text.replace(old, new, 1))

path = root / "src/target/riscv64/codegen_expression.c"
text = path.read_text()
start_marker = "static bool minic_riscv64_emit_builtin_unary(FILE *file,\n"
end_marker = "static bool minic_riscv64_emit_overflow_builtin(FILE *file,\n"
start = text.find(start_marker)
end = text.find(end_marker, start + 1)
if start < 0 or end < 0 or end <= start:
    raise SystemExit("cannot locate generated builtin unary lowering")
replacement = r'''static bool minic_riscv64_emit_builtin_unary(FILE *file,
                                             const MinicC0Program *program,
                                             const MinicFunction *function,
                                             const MinicExpression *expression,
                                             MinicExpressionId expression_id) {
    const MinicExpression *operand;

    if (file == NULL || program == NULL || expression == NULL ||
        expression->kind != MINIC_EXPRESSION_BUILTIN_UNARY ||
        expression->value.builtin_unary.operator_kind != MINIC_BUILTIN_UNARY_CLZLL ||
        !minic_type_equal(expression->type, minic_type_int())) {
        return false;
    }
    operand = minic_c0_program_expression(program, expression->value.builtin_unary.operand);
    if (operand == NULL ||
        !minic_type_equal(operand->type, minic_type_unsigned_long_long()) ||
        !minic_riscv64_emit_expression(
            file, program, function, expression->value.builtin_unary.operand)) {
        return false;
    }

    /* __builtin_clzll(0) is undefined. For non-zero values this baseline RV64I
     * binary search computes the exact count without requiring Zbb clz. */
    return fprintf(file,
                   "  li t0, 0\n"
                   "  srli t1, a0, 32\n"
                   "  bnez t1, .Lminic_clzll_32_%zu\n"
                   "  addi t0, t0, 32\n"
                   "  slli a0, a0, 32\n"
                   ".Lminic_clzll_32_%zu:\n"
                   "  srli t1, a0, 48\n"
                   "  bnez t1, .Lminic_clzll_16_%zu\n"
                   "  addi t0, t0, 16\n"
                   "  slli a0, a0, 16\n"
                   ".Lminic_clzll_16_%zu:\n"
                   "  srli t1, a0, 56\n"
                   "  bnez t1, .Lminic_clzll_8_%zu\n"
                   "  addi t0, t0, 8\n"
                   "  slli a0, a0, 8\n"
                   ".Lminic_clzll_8_%zu:\n"
                   "  srli t1, a0, 60\n"
                   "  bnez t1, .Lminic_clzll_4_%zu\n"
                   "  addi t0, t0, 4\n"
                   "  slli a0, a0, 4\n"
                   ".Lminic_clzll_4_%zu:\n"
                   "  srli t1, a0, 62\n"
                   "  bnez t1, .Lminic_clzll_2_%zu\n"
                   "  addi t0, t0, 2\n"
                   "  slli a0, a0, 2\n"
                   ".Lminic_clzll_2_%zu:\n"
                   "  srli t1, a0, 63\n"
                   "  bnez t1, .Lminic_clzll_1_%zu\n"
                   "  addi t0, t0, 1\n"
                   ".Lminic_clzll_1_%zu:\n"
                   "  mv a0, t0\n",
                   expression_id,
                   expression_id,
                   expression_id,
                   expression_id,
                   expression_id,
                   expression_id,
                   expression_id,
                   expression_id,
                   expression_id,
                   expression_id,
                   expression_id,
                   expression_id) >= 0;
}

'''
path.write_text(text[:start] + replacement + text[end:])
