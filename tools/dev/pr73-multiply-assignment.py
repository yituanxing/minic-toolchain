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
    "src/frontend/token.h",
    '''    MINIC_TOKEN_ARROW,\n    MINIC_TOKEN_STAR,\n    MINIC_TOKEN_AMPERSAND,\n''',
    '''    MINIC_TOKEN_ARROW,\n    MINIC_TOKEN_STAR,\n    MINIC_TOKEN_STAR_EQUAL,\n    MINIC_TOKEN_AMPERSAND,\n''',
)

replace_once(
    "src/frontend/token.c",
    '''    case MINIC_TOKEN_STAR:\n        return "*";\n    case MINIC_TOKEN_AMPERSAND:\n''',
    '''    case MINIC_TOKEN_STAR:\n        return "*";\n    case MINIC_TOKEN_STAR_EQUAL:\n        return "*=";\n    case MINIC_TOKEN_AMPERSAND:\n''',
)

replace_once(
    "src/frontend/lexer.c",
    '''    case '*':\n        token->kind = MINIC_TOKEN_STAR;\n        break;\n''',
    '''    case '*':\n        if (minic_lexer_peek_next(lexer) == '=') {\n            token->kind = MINIC_TOKEN_STAR_EQUAL;\n            minic_lexer_advance(lexer);\n        } else {\n            token->kind = MINIC_TOKEN_STAR;\n        }\n        break;\n''',
)

replace_once(
    "src/frontend/parser_statement.c",
    '''        assignment_token != MINIC_TOKEN_PLUS_EQUAL && assignment_token != MINIC_TOKEN_MINUS_EQUAL &&\n        assignment_token != MINIC_TOKEN_AMPERSAND_EQUAL &&\n''',
    '''        assignment_token != MINIC_TOKEN_PLUS_EQUAL && assignment_token != MINIC_TOKEN_MINUS_EQUAL &&\n        assignment_token != MINIC_TOKEN_STAR_EQUAL &&\n        assignment_token != MINIC_TOKEN_AMPERSAND_EQUAL &&\n''',
)

old = r'''    } else if (assignment_token == MINIC_TOKEN_MINUS_EQUAL) {
        const MinicExpression *right_expression;
        MinicExpression subtraction;
        MinicExpressionId right_id;
        MinicType common_type;

        right_id = statement.expression;
        right_expression = minic_c0_program_expression(parser->program, right_id);
        if (right_expression == NULL || !minic_type_is_integer(first_type) ||
            !minic_type_is_integer(right_expression->type) ||
            !minic_type_integer_common(first_type, right_expression->type, &common_type)) {
            minic_parser_error(parser, "compound subtraction assignment requires integer operands");
            return false;
        }
        (void)memset(&subtraction, 0, sizeof(subtraction));
        subtraction.kind = MINIC_EXPRESSION_BINARY;
        subtraction.span.begin = statement.span.begin;
        subtraction.span.end = right_expression->span.end;
        subtraction.type = common_type;
        subtraction.value_category = MINIC_VALUE_RVALUE;
        subtraction.value.binary.operator_kind = MINIC_BINARY_SUBTRACT;
        subtraction.value.binary.left = statement.target_expression;
        subtraction.value.binary.right = right_id;
        if (!minic_parser_add_expression(parser, &subtraction, &statement.expression)) {
            return false;
        }
    } else if (assignment_token == MINIC_TOKEN_GREATER_GREATER_EQUAL) {
'''
new = r'''    } else if (assignment_token == MINIC_TOKEN_MINUS_EQUAL ||
               assignment_token == MINIC_TOKEN_STAR_EQUAL) {
        const MinicExpression *right_expression;
        MinicExpression arithmetic;
        MinicExpressionId right_id;
        MinicType common_type;

        right_id = statement.expression;
        right_expression = minic_c0_program_expression(parser->program, right_id);
        if (right_expression == NULL || !minic_type_is_integer(first_type) ||
            !minic_type_is_integer(right_expression->type) ||
            !minic_type_integer_common(first_type, right_expression->type, &common_type)) {
            minic_parser_error(parser,
                               assignment_token == MINIC_TOKEN_STAR_EQUAL
                                   ? "compound multiplication assignment requires integer operands"
                                   : "compound subtraction assignment requires integer operands");
            return false;
        }
        (void)memset(&arithmetic, 0, sizeof(arithmetic));
        arithmetic.kind = MINIC_EXPRESSION_BINARY;
        arithmetic.span.begin = statement.span.begin;
        arithmetic.span.end = right_expression->span.end;
        arithmetic.type = common_type;
        arithmetic.value_category = MINIC_VALUE_RVALUE;
        arithmetic.value.binary.operator_kind = assignment_token == MINIC_TOKEN_STAR_EQUAL
                                                     ? MINIC_BINARY_MULTIPLY
                                                     : MINIC_BINARY_SUBTRACT;
        arithmetic.value.binary.left = statement.target_expression;
        arithmetic.value.binary.right = right_id;
        if (!minic_parser_add_expression(parser, &arithmetic, &statement.expression)) {
            return false;
        }
    } else if (assignment_token == MINIC_TOKEN_GREATER_GREATER_EQUAL) {
'''
replace_once("src/frontend/parser_statement.c", old, new)

print("staged integer *= compound assignment")
