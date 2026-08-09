#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:160]!r}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/ast.h",
    '''    MINIC_BINARY_LOGICAL_AND,
    MINIC_BINARY_LOGICAL_OR
} MinicBinaryOperator;
''',
    '''    MINIC_BINARY_LOGICAL_AND,
    MINIC_BINARY_LOGICAL_OR,
    MINIC_BINARY_COMMA
} MinicBinaryOperator;
''',
)

replace_once(
    "src/frontend/parser_expression.c",
    '''static bool parse_expression_internal(MinicParser *parser,
                                      MinicExpressionId *expression_id,
                                      unsigned int minimum_precedence,
                                      bool decay_array);
''',
    '''static bool parse_expression_internal(MinicParser *parser,
                                      MinicExpressionId *expression_id,
                                      unsigned int minimum_precedence,
                                      bool decay_array);
static bool parse_comma_expression(MinicParser *parser,
                                   MinicExpressionId *expression_id,
                                   bool decay_array);
''',
)

replace_once(
    "src/frontend/parser_expression.c",
    '''    if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        if (!minic_parser_advance(parser) ||
            !parse_expression_internal(parser, &primary_id, 0U, decay_array) ||
            !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')'")) {
            return false;
        }
''',
    '''    if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        if (!minic_parser_advance(parser) ||
            !parse_comma_expression(parser, &primary_id, decay_array) ||
            !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')'")) {
            return false;
        }
''',
)

marker = '''bool minic_parser_parse_expression(MinicParser *parser,
                                   MinicExpressionId *expression_id,
                                   unsigned int minimum_precedence) {
'''
helper = r'''static bool parse_comma_expression(MinicParser *parser,
                                   MinicExpressionId *expression_id,
                                   bool decay_array) {
    MinicExpressionId left;

    if (!parse_expression_internal(parser, &left, 0U, decay_array)) {
        return false;
    }
    while (parser->current.kind == MINIC_TOKEN_COMMA) {
        MinicExpression sequence;
        MinicExpressionId right;
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;

        if (!minic_parser_advance(parser) ||
            !parse_expression_internal(parser, &right, 0U, decay_array)) {
            return false;
        }
        left_expression = minic_c0_program_expression(parser->program, left);
        right_expression = minic_c0_program_expression(parser->program, right);
        if (left_expression == NULL || right_expression == NULL) {
            minic_parser_error(parser, "invalid comma expression operands");
            return false;
        }

        (void)memset(&sequence, 0, sizeof(sequence));
        sequence.kind = MINIC_EXPRESSION_BINARY;
        sequence.span.begin = left_expression->span.begin;
        sequence.span.end = right_expression->span.end;
        sequence.type = right_expression->type;
        sequence.value_category = MINIC_VALUE_RVALUE;
        sequence.value.binary.operator_kind = MINIC_BINARY_COMMA;
        sequence.value.binary.left = left;
        sequence.value.binary.right = right;
        if (!minic_parser_add_expression(parser, &sequence, &left)) {
            return false;
        }
    }
    *expression_id = left;
    return true;
}

'''
path = Path("src/frontend/parser_expression.c")
text = path.read_text()
if text.count(marker) != 1:
    raise SystemExit("cannot locate public expression parser")
path.write_text(text.replace(marker, helper + marker, 1))

replace_once(
    "src/frontend/ast_verifier.c",
    '''static bool binary_operator_is_valid(MinicBinaryOperator operator_kind) {
    return operator_kind >= MINIC_BINARY_ADD && operator_kind <= MINIC_BINARY_LOGICAL_OR;
}
''',
    '''static bool binary_operator_is_valid(MinicBinaryOperator operator_kind) {
    return operator_kind >= MINIC_BINARY_ADD && operator_kind <= MINIC_BINARY_COMMA;
}
''',
)

replace_once(
    "src/frontend/ast_verifier.c",
    '''    if (!binary_operator_is_valid(expression->value.binary.operator_kind)) {
        return false;
    }

    if (binary_is_logical(expression->value.binary.operator_kind)) {
''',
    '''    if (!binary_operator_is_valid(expression->value.binary.operator_kind)) {
        return false;
    }

    if (expression->value.binary.operator_kind == MINIC_BINARY_COMMA) {
        return minic_type_equal(expression->type, right->type);
    }

    if (binary_is_logical(expression->value.binary.operator_kind)) {
''',
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    '''        if (expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_AND ||
            expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_OR) {
            return minic_riscv64_emit_logical_binary(
                file, program, function, expression, expression_id);
        }
''',
    '''        if (expression->value.binary.operator_kind == MINIC_BINARY_COMMA) {
            return minic_riscv64_emit_expression(
                       file, program, function, expression->value.binary.left) &&
                   minic_riscv64_emit_expression(
                       file, program, function, expression->value.binary.right);
        }
        if (expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_AND ||
            expression->value.binary.operator_kind == MINIC_BINARY_LOGICAL_OR) {
            return minic_riscv64_emit_logical_binary(
                file, program, function, expression, expression_id);
        }
''',
)

replace_once(
    "src/target/riscv64/codegen_expression.c",
    '''        case MINIC_BINARY_LOGICAL_AND:
        case MINIC_BINARY_LOGICAL_OR:
            return false;
        }
''',
    '''        case MINIC_BINARY_LOGICAL_AND:
        case MINIC_BINARY_LOGICAL_OR:
        case MINIC_BINARY_COMMA:
            return false;
        }
''',
)

print("staged parenthesized comma operator sequencing")
