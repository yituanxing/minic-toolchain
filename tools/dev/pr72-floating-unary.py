#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    if old not in text:
        raise SystemExit(f"missing expected text in {path}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/parser_expression.c",
    '''    if (!minic_type_is_integer(operand_expression->type)) {
        minic_parser_error(parser, "unary arithmetic requires an integer operand");
        return false;
    }
    if (operator_token.kind == MINIC_TOKEN_TILDE) {
''',
    '''    if (minic_type_is_double(operand_expression->type)) {
        if (operator_token.kind != MINIC_TOKEN_PLUS && operator_token.kind != MINIC_TOKEN_MINUS) {
            minic_parser_error(parser, "floating unary arithmetic requires '+' or '-'");
            return false;
        }
        expression.value.unary.operator_kind =
            operator_token.kind == MINIC_TOKEN_PLUS ? MINIC_UNARY_PLUS : MINIC_UNARY_NEGATE;
        expression.type = operand_expression->type;
        return minic_parser_add_expression(parser, &expression, expression_id);
    }
    if (!minic_type_is_integer(operand_expression->type)) {
        minic_parser_error(parser, "unary arithmetic requires an integer or double operand");
        return false;
    }
    if (operator_token.kind == MINIC_TOKEN_TILDE) {
''',
)

replace_once(
    "src/frontend/ast_verifier.c",
    '''        if (!minic_type_is_integer(operand->type) ||
            !minic_type_integer_promotion(operand->type, &expected_type)) {
            return false;
        }
        return minic_type_equal(expression->type, expected_type);
''',
    '''        if ((expression->value.unary.operator_kind == MINIC_UNARY_PLUS ||
             expression->value.unary.operator_kind == MINIC_UNARY_NEGATE) &&
            minic_type_is_double(operand->type)) {
            return minic_type_equal(expression->type, operand->type);
        }
        if (!minic_type_is_integer(operand->type) ||
            !minic_type_integer_promotion(operand->type, &expected_type)) {
            return false;
        }
        return minic_type_equal(expression->type, expected_type);
''',
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    '''        case MINIC_UNARY_PLUS:
            return minic_riscv64_emit_normalize_integer(file, expression->type, "a0");
        case MINIC_UNARY_NEGATE:
            return fprintf(file,
                           minic_type_is_long_integer(expression->type) ? "  neg a0, a0\\n"
                                                                        : "  negw a0, a0\\n") >= 0 &&
                   minic_riscv64_emit_normalize_integer(file, expression->type, "a0");
''',
    '''        case MINIC_UNARY_PLUS:
            if (minic_type_is_double(expression->type)) {
                return true;
            }
            return minic_riscv64_emit_normalize_integer(file, expression->type, "a0");
        case MINIC_UNARY_NEGATE:
            if (minic_type_is_double(expression->type)) {
                return fprintf(file,
                               "  fmv.d.x ft0, a0\\n"
                               "  fneg.d ft0, ft0\\n"
                               "  fmv.x.d a0, ft0\\n") >= 0;
            }
            return fprintf(file,
                           minic_type_is_long_integer(expression->type) ? "  neg a0, a0\\n"
                                                                        : "  negw a0, a0\\n") >= 0 &&
                   minic_riscv64_emit_normalize_integer(file, expression->type, "a0");
''',
)

print("staged double unary plus/minus")
