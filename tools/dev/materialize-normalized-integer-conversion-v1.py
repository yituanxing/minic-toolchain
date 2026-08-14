#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def replace_once(path: str, old: str, new: str) -> None:
    file_path = ROOT / path
    text = file_path.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}")
    file_path.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/cast_normalization.c",
    '''    if (minic_type_is_integer(cast_expression->type) &&\n        minic_type_is_integer(operand_expression->type)) {\n        MinicExpression zero_expression;\n        MinicExpression normalized_expression;\n        MinicExpressionId zero_id;\n\n        (void)memset(&zero_expression, 0, sizeof(zero_expression));\n        zero_expression.kind = MINIC_EXPRESSION_INTEGER;\n        zero_expression.span = cast_expression->span;\n        zero_expression.type = minic_type_int();\n        zero_expression.value_category = MINIC_VALUE_RVALUE;\n        zero_expression.value.integer_value = 0;\n        if (!minic_c0_program_add_expression(rewritten, &zero_expression, &zero_id)) {\n            return false;\n        }\n\n        (void)memset(&normalized_expression, 0, sizeof(normalized_expression));\n        normalized_expression.kind = MINIC_EXPRESSION_BINARY;\n        normalized_expression.span = cast_expression->span;\n        normalized_expression.type = cast_expression->type;\n        normalized_expression.value_category = MINIC_VALUE_RVALUE;\n        normalized_expression.value.binary.operator_kind = MINIC_BINARY_ADD;\n        normalized_expression.value.binary.left = mapped_operand;\n        normalized_expression.value.binary.right = zero_id;\n        return minic_c0_program_add_expression(rewritten, &normalized_expression, normalized_id);\n    }\n''',
    '''    if (minic_type_is_integer(cast_expression->type) &&\n        minic_type_is_integer(operand_expression->type)) {\n        return append_normalized_conversion(\n            rewritten, cast_expression, mapped_operand, normalized_id);\n    }\n''',
)

replace_once(
    "src/frontend/ast_verifier.c",
    '''static bool is_normalized_integer_cast_add(const MinicExpression *expression,\n                                           const MinicExpression *left,\n                                           const MinicExpression *right,\n                                           MinicC0AstForm form) {\n    return form == MINIC_C0_AST_NORMALIZED &&\n           expression->value.binary.operator_kind == MINIC_BINARY_ADD &&\n           right->kind == MINIC_EXPRESSION_INTEGER && right->value.integer_value == 0 &&\n           minic_type_equal(right->type, minic_type_int()) && minic_type_is_integer(left->type) &&\n           minic_type_is_integer(expression->type) &&\n           minic_type_cast_compatible(expression->type, left->type);\n}\n\n''',
    "",
)

replace_once(
    "src/frontend/ast_verifier.c",
    '''static bool verify_binary_type(const MinicC0Program *program,\n                               const MinicTargetInfo *target,\n                               const MinicExpression *expression,\n                               const MinicExpression *left,\n                               const MinicExpression *right,\n                               MinicC0AstForm form) {\n''',
    '''static bool verify_binary_type(const MinicC0Program *program,\n                               const MinicTargetInfo *target,\n                               const MinicExpression *expression,\n                               const MinicExpression *left,\n                               const MinicExpression *right) {\n''',
)

replace_once(
    "src/frontend/ast_verifier.c",
    '''        return minic_type_equal(expression->type, expected_type) ||\n               is_normalized_integer_cast_add(expression, left, right, form);\n''',
    '''        return minic_type_equal(expression->type, expected_type);\n''',
)

replace_once(
    "src/frontend/ast_verifier.c",
    '''               verify_binary_type(program, target, expression, left, right, form);\n''',
    '''               verify_binary_type(program, target, expression, left, right);\n''',
)

replace_once(
    "src/frontend/ast_verifier.c",
    '''               ((minic_type_is_double(expression->type) &&\n                 (minic_type_is_integer(operand->type) || minic_type_is_float(operand->type))) ||\n                (minic_type_is_integer(expression->type) && minic_type_is_double(operand->type)));\n''',
    '''               ((minic_type_is_double(expression->type) &&\n                 (minic_type_is_integer(operand->type) || minic_type_is_float(operand->type))) ||\n                (minic_type_is_integer(expression->type) && minic_type_is_double(operand->type)) ||\n                (minic_type_is_integer(expression->type) && minic_type_is_integer(operand->type) &&\n                 minic_type_cast_compatible(expression->type, operand->type)));\n''',
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    '''        if (operand == NULL) {\n            return false;\n        }\n        if (minic_type_is_double(expression->type) && minic_type_is_float(operand->type)) {\n''',
    '''        if (operand == NULL) {\n            return false;\n        }\n        if (minic_type_is_integer(expression->type) && minic_type_is_integer(operand->type)) {\n            return minic_riscv64_emit_expression(\n                       file, program, function, function_layout, expression->value.unary.operand) &&\n                   minic_riscv64_emit_integer_conversion(file, expression->type, "a0");\n        }\n        if (minic_type_is_double(expression->type) && minic_type_is_float(operand->type)) {\n''',
)

replace_once(
    "tests/frontend/ast_contract_test.c",
    '''    if (program.expression_count != 3U || statement == NULL || normalized == NULL ||\n        normalized->kind != MINIC_EXPRESSION_BINARY ||\n        normalized->value.binary.operator_kind != MINIC_BINARY_ADD ||\n        normalized->value.binary.left >= statement->expression ||\n        normalized->value.binary.right >= statement->expression ||\n        !minic_type_equal(normalized->type, minic_type_unsigned_char())) {\n''',
    '''    if (program.expression_count != 2U || statement == NULL || normalized == NULL ||\n        normalized->kind != MINIC_EXPRESSION_CONVERSION ||\n        normalized->value.unary.operand >= statement->expression ||\n        normalized->value.unary.operand != integer_id ||\n        !minic_type_equal(normalized->type, minic_type_unsigned_char())) {\n''',
)

replace_once(
    "tests/compiler/c0/run-cast-expressions.sh",
    '''printf '%s\\n' "PASS compiler/c0/cast_integer_lowering"\n\ncompile_success cast_typedef_shadow\n''',
    '''printf '%s\\n' "PASS compiler/c0/cast_integer_lowering"\n\ncompile_success cast_integer_conversion\ngrep -F "  addiw a0, a0, 0" "$work/cast_integer_conversion.s" >/dev/null\nif grep -F "  addw a0, t0, a0" "$work/cast_integer_conversion.s" >/dev/null; then\n    printf '%s\\n' \\\n        "FAIL compiler/c0/cast_integer_conversion: integer cast lowered as synthetic addition" >&2\n    exit 1\nfi\nprintf '%s\\n' "PASS compiler/c0/cast_integer_conversion normalized=conversion"\n\ncompile_success cast_typedef_shadow\n''',
)

fixture = ROOT / "tests/compiler/c0/cast_integer_conversion.c"
if fixture.exists():
    raise SystemExit("cast_integer_conversion.c already exists")
fixture.write_text(
    '''int main(void)\n{\n    unsigned int unsigned_value;\n    int signed_value;\n\n    unsigned_value = (unsigned int)-1;\n    signed_value = (int)unsigned_value;\n    return signed_value == -1 ? 0 : 1;\n}\n'''
)

verifier = (ROOT / "src/frontend/ast_verifier.c").read_text()
normalizer = (ROOT / "src/frontend/cast_normalization.c").read_text()
contract = (ROOT / "tests/frontend/ast_contract_test.c").read_text()
if "is_normalized_integer_cast_add" in verifier:
    raise SystemExit("legacy normalized integer-cast binary-add verifier remains")
if "normalized_expression.value.binary.operator_kind = MINIC_BINARY_ADD" in normalizer:
    raise SystemExit("legacy normalized integer-cast binary-add construction remains")
if "normalized->kind != MINIC_EXPRESSION_BINARY" in contract:
    raise SystemExit("AST contract still expects integer casts as binary expressions")
print("PASS materialize-normalized-integer-conversion-v1")
