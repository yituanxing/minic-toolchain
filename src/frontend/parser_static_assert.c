#include "frontend/parser_internal.h"

#include <stdint.h>

bool minic_parser_parse_static_assert_declaration(MinicParser *parser) {
    const MinicExpression *condition;
    MinicExpressionId condition_id;
    int64_t condition_value;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_KW_STATIC_ASSERT) {
        if (parser != NULL) {
            minic_parser_error(parser, "expected _Static_assert declaration");
        }
        return false;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after _Static_assert") ||
        !minic_parser_parse_expression(parser, &condition_id, 0U)) {
        return false;
    }
    condition = minic_c0_program_expression(parser->program, condition_id);
    if (condition == NULL || !minic_type_is_integer(condition->type) ||
        !minic_parser_evaluate_integer_constant_expression(
            parser->program, condition_id, &condition_value)) {
        minic_parser_error(parser,
                           "_Static_assert condition must be an integer constant expression");
        return false;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_COMMA, "expected ',' in _Static_assert") ||
        parser->current.kind != MINIC_TOKEN_STRING_LITERAL) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "expected string literal in _Static_assert");
        }
        return false;
    }
    do {
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } while (parser->current.kind == MINIC_TOKEN_STRING_LITERAL);
    if (!minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after _Static_assert") ||
        !minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after _Static_assert")) {
        return false;
    }
    if (condition_value == 0) {
        minic_parser_error(parser, "static assertion failed");
        return false;
    }
    return true;
}
