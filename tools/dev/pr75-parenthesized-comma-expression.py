#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:120]!r}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/ast.h",
    """    MINIC_BINARY_LOGICAL_AND,
    MINIC_BINARY_LOGICAL_OR
} MinicBinaryOperator;
""",
    """    MINIC_BINARY_LOGICAL_AND,
    MINIC_BINARY_LOGICAL_OR,
    MINIC_BINARY_COMMA
} MinicBinaryOperator;
""",
)

replace_once(
    "src/frontend/parser_expression.c",
    """    if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        if (!minic_parser_advance(parser) ||
            !parse_expression_internal(parser, &primary_id, 0U, decay_array) ||
            !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, \"expected ')'\")) {
            return false;
        }
        if (!minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
""",
    """    if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        if (!minic_parser_advance(parser) ||
            !parse_expression_internal(parser, &primary_id, 0U, decay_array)) {
            return false;
        }
        while (parser->current.kind == MINIC_TOKEN_COMMA) {
            const MinicExpression *left_expression;
            const MinicExpression *right_expression;
            MinicExpression comma_expression;
            MinicExpressionId right_id;

            left_expression = minic_c0_program_expression(parser->program, primary_id);
            if (left_expression == NULL || !minic_parser_advance(parser) ||
                !parse_expression_internal(parser, &right_id, 0U, true)) {
                return false;
            }
            right_expression = minic_c0_program_expression(parser->program, right_id);
            if (right_expression == NULL) {
                minic_parser_error(parser, \"invalid comma expression operand\");
                return false;
            }
            (void)memset(&comma_expression, 0, sizeof(comma_expression));
            comma_expression.kind = MINIC_EXPRESSION_BINARY;
            comma_expression.span.begin = left_expression->span.begin;
            comma_expression.span.end = right_expression->span.end;
            comma_expression.type = right_expression->type;
            comma_expression.value_category = MINIC_VALUE_RVALUE;
            comma_expression.value.binary.operator_kind = MINIC_BINARY_COMMA;
            comma_expression.value.binary.left = primary_id;
            comma_expression.value.binary.right = right_id;
            if (!minic_parser_add_expression(parser, &comma_expression, &primary_id)) {
                return false;
            }
        }
        if (!minic_parser_expect(parser, MINIC_TOKEN_RPAREN, \"expected ')'\") ||
            !minic_parser_parse_postfix(parser, primary_id, &primary_id)) {
            return false;
        }
        return finish_value_expression(parser, primary_id, decay_array, expression_id);
    }
""",
)

replace_once(
    "src/frontend/ast_verifier.c",
    """static bool binary_operator_is_valid(MinicBinaryOperator operator_kind) {
    return operator_kind >= MINIC_BINARY_ADD && operator_kind <= MINIC_BINARY_LOGICAL_OR;
}
""",
    """static bool binary_operator_is_valid(MinicBinaryOperator operator_kind) {
    return operator_kind >= MINIC_BINARY_ADD && operator_kind <= MINIC_BINARY_COMMA;
}
""",
)

replace_once(
    "src/frontend/ast_verifier.c",
    """    if (!binary_operator_is_valid(expression->value.binary.operator_kind)) {
        return false;
    }

    if (binary_is_logical(expression->value.binary.operator_kind)) {
""",
    """    if (!binary_operator_is_valid(expression->value.binary.operator_kind)) {
        return false;
    }

    if (expression->value.binary.operator_kind == MINIC_BINARY_COMMA) {
        return expression->value_category == MINIC_VALUE_RVALUE &&
               minic_type_equal(expression->type, right->type);
    }

    if (binary_is_logical(expression->value.binary.operator_kind)) {
""",
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    """        if (expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_AND ||
            expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_OR) {
            return minic_riscv64_emit_logical_binary(
                file, program, function, expression, expression_id);
        }

        left = minic_c0_program_expression(program, expression->value.binary.left);
""",
    """        if (expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_AND ||
            expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_OR) {
            return minic_riscv64_emit_logical_binary(
                file, program, function, expression, expression_id);
        }
        if (expression->value.binary.operator_kind == MINIC_BINARY_COMMA) {
            left = minic_c0_program_expression(program, expression->value.binary.left);
            right = minic_c0_program_expression(program, expression->value.binary.right);
            return left != NULL && right != NULL &&
                   minic_riscv64_emit_expression(
                       file, program, function, expression->value.binary.left) &&
                   minic_riscv64_emit_expression(
                       file, program, function, expression->value.binary.right);
        }

        left = minic_c0_program_expression(program, expression->value.binary.left);
""",
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    """        case MINIC_BINARY_LOGICAL_AND:
        case MINIC_BINARY_LOGICAL_OR:
            return false;
""",
    """        case MINIC_BINARY_LOGICAL_AND:
        case MINIC_BINARY_LOGICAL_OR:
        case MINIC_BINARY_COMMA:
            return false;
""",
)

print("staged parenthesized comma expressions with left-to-right side effects")
