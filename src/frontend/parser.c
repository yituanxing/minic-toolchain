#include "frontend/parser.h"

#include "frontend/lexer.h"
#include "frontend/token.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct MinicParser {
    const char *path;
    const char *source;
    MinicLexer lexer;
    MinicToken current;
    MinicDiagnostic *diagnostic;
    MinicC0Program *program;
} MinicParser;

static void minic_parser_set_diagnostic(MinicParser *parser, const char *format, ...)
{
    va_list arguments;

    if (parser->diagnostic == NULL) {
        return;
    }
    parser->diagnostic->path = parser->path;
    parser->diagnostic->line = parser->current.span.begin.line;
    parser->diagnostic->column = parser->current.span.begin.column;
    va_start(arguments, format);
    (void)vsnprintf(
        parser->diagnostic->message,
        sizeof(parser->diagnostic->message),
        format,
        arguments);
    va_end(arguments);
}

static bool minic_parser_advance(MinicParser *parser)
{
    return minic_lexer_next(&parser->lexer, &parser->current, parser->diagnostic);
}

static bool minic_parser_initialize(
    MinicParser *parser,
    const char *path,
    const char *source,
    size_t length,
    MinicC0Program *program,
    MinicDiagnostic *diagnostic)
{
    parser->path = path;
    parser->source = source;
    parser->diagnostic = diagnostic;
    parser->program = program;
    minic_lexer_initialize(&parser->lexer, path, source, length);
    return minic_parser_advance(parser);
}

static bool minic_parser_token_text_equals(
    const MinicParser *parser,
    const char *expected)
{
    size_t token_length;
    size_t expected_length;

    token_length = parser->current.span.end.offset -
                   parser->current.span.begin.offset;
    expected_length = strlen(expected);
    return token_length == expected_length &&
           memcmp(
               parser->source + parser->current.span.begin.offset,
               expected,
               token_length) == 0;
}

static bool minic_parser_expect(
    MinicParser *parser,
    MinicTokenKind expected,
    const char *message)
{
    if (parser->current.kind != expected) {
        minic_parser_set_diagnostic(parser, "%s", message);
        return false;
    }
    return minic_parser_advance(parser);
}

static bool minic_parser_expect_main(MinicParser *parser)
{
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER ||
        !minic_parser_token_text_equals(parser, "main")) {
        minic_parser_set_diagnostic(parser, "expected identifier 'main'");
        return false;
    }
    return minic_parser_advance(parser);
}

static bool minic_parser_add_expression(
    MinicParser *parser,
    const MinicExpression *expression,
    MinicExpressionId *expression_id)
{
    if (minic_c0_program_add_expression(
            parser->program,
            expression,
            expression_id)) {
        return true;
    }
    minic_parser_set_diagnostic(
        parser,
        "out of memory while building expression tree");
    return false;
}

static bool minic_parser_parse_expression(
    MinicParser *parser,
    MinicExpressionId *expression_id);

static bool minic_parser_parse_integer(
    MinicParser *parser,
    MinicExpressionId *expression_id)
{
    MinicExpression expression;
    size_t offset;
    size_t end;
    unsigned long result;

    if (parser->current.kind != MINIC_TOKEN_INTEGER_CONSTANT) {
        minic_parser_set_diagnostic(parser, "expected expression");
        return false;
    }

    expression.kind = MINIC_EXPRESSION_INTEGER;
    expression.span = parser->current.span;
    offset = parser->current.span.begin.offset;
    end = parser->current.span.end.offset;
    result = 0UL;
    while (offset < end) {
        unsigned long digit;

        digit = (unsigned long)(unsigned int)(parser->source[offset] - '0');
        if (result > ((unsigned long)INT_MAX - digit) / 10UL) {
            minic_parser_set_diagnostic(
                parser,
                "integer constant exceeds C0 int range");
            return false;
        }
        result = result * 10UL + digit;
        offset += 1U;
    }
    expression.value.integer_value = (int)result;
    if (!minic_parser_add_expression(parser, &expression, expression_id)) {
        return false;
    }
    return minic_parser_advance(parser);
}

static bool minic_parser_parse_primary(
    MinicParser *parser,
    MinicExpressionId *expression_id)
{
    if (parser->current.kind == MINIC_TOKEN_INTEGER_CONSTANT) {
        return minic_parser_parse_integer(parser, expression_id);
    }
    if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_expression(parser, expression_id) ||
            !minic_parser_expect(
                parser,
                MINIC_TOKEN_RPAREN,
                "expected ')'")) {
            return false;
        }
        return true;
    }
    minic_parser_set_diagnostic(parser, "expected expression");
    return false;
}

static bool minic_parser_parse_unary(
    MinicParser *parser,
    MinicExpressionId *expression_id)
{
    MinicToken operator_token;
    MinicExpression expression;
    MinicExpressionId operand;
    const MinicExpression *operand_expression;

    if (parser->current.kind != MINIC_TOKEN_PLUS &&
        parser->current.kind != MINIC_TOKEN_MINUS) {
        return minic_parser_parse_primary(parser, expression_id);
    }

    operator_token = parser->current;
    if (!minic_parser_advance(parser) ||
        !minic_parser_parse_unary(parser, &operand)) {
        return false;
    }
    operand_expression = minic_c0_program_expression(parser->program, operand);
    if (operand_expression == NULL) {
        minic_parser_set_diagnostic(parser, "invalid unary operand");
        return false;
    }

    expression.kind = MINIC_EXPRESSION_UNARY;
    expression.span.begin = operator_token.span.begin;
    expression.span.end = operand_expression->span.end;
    expression.value.unary.operator_kind =
        operator_token.kind == MINIC_TOKEN_PLUS
            ? MINIC_UNARY_PLUS
            : MINIC_UNARY_NEGATE;
    expression.value.unary.operand = operand;
    return minic_parser_add_expression(parser, &expression, expression_id);
}

static bool minic_parser_parse_multiplicative(
    MinicParser *parser,
    MinicExpressionId *expression_id)
{
    MinicExpressionId left;

    if (!minic_parser_parse_unary(parser, &left)) {
        return false;
    }
    while (parser->current.kind == MINIC_TOKEN_STAR ||
           parser->current.kind == MINIC_TOKEN_SLASH ||
           parser->current.kind == MINIC_TOKEN_PERCENT) {
        MinicToken operator_token;
        MinicExpression expression;
        MinicExpressionId right;
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;

        operator_token = parser->current;
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_unary(parser, &right)) {
            return false;
        }
        left_expression = minic_c0_program_expression(parser->program, left);
        right_expression = minic_c0_program_expression(parser->program, right);
        if (left_expression == NULL || right_expression == NULL) {
            minic_parser_set_diagnostic(parser, "invalid binary operand");
            return false;
        }

        expression.kind = MINIC_EXPRESSION_BINARY;
        expression.span.begin = left_expression->span.begin;
        expression.span.end = right_expression->span.end;
        expression.value.binary.left = left;
        expression.value.binary.right = right;
        if (operator_token.kind == MINIC_TOKEN_STAR) {
            expression.value.binary.operator_kind = MINIC_BINARY_MULTIPLY;
        } else if (operator_token.kind == MINIC_TOKEN_SLASH) {
            expression.value.binary.operator_kind = MINIC_BINARY_DIVIDE;
        } else {
            expression.value.binary.operator_kind = MINIC_BINARY_REMAINDER;
        }
        if (!minic_parser_add_expression(parser, &expression, &left)) {
            return false;
        }
    }
    *expression_id = left;
    return true;
}

static bool minic_parser_parse_additive(
    MinicParser *parser,
    MinicExpressionId *expression_id)
{
    MinicExpressionId left;

    if (!minic_parser_parse_multiplicative(parser, &left)) {
        return false;
    }
    while (parser->current.kind == MINIC_TOKEN_PLUS ||
           parser->current.kind == MINIC_TOKEN_MINUS) {
        MinicToken operator_token;
        MinicExpression expression;
        MinicExpressionId right;
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;

        operator_token = parser->current;
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_multiplicative(parser, &right)) {
            return false;
        }
        left_expression = minic_c0_program_expression(parser->program, left);
        right_expression = minic_c0_program_expression(parser->program, right);
        if (left_expression == NULL || right_expression == NULL) {
            minic_parser_set_diagnostic(parser, "invalid binary operand");
            return false;
        }

        expression.kind = MINIC_EXPRESSION_BINARY;
        expression.span.begin = left_expression->span.begin;
        expression.span.end = right_expression->span.end;
        expression.value.binary.operator_kind =
            operator_token.kind == MINIC_TOKEN_PLUS
                ? MINIC_BINARY_ADD
                : MINIC_BINARY_SUBTRACT;
        expression.value.binary.left = left;
        expression.value.binary.right = right;
        if (!minic_parser_add_expression(parser, &expression, &left)) {
            return false;
        }
    }
    *expression_id = left;
    return true;
}

static bool minic_parser_parse_expression(
    MinicParser *parser,
    MinicExpressionId *expression_id)
{
    return minic_parser_parse_additive(parser, expression_id);
}

bool minic_parse_c0_program(
    const char *path,
    const char *source,
    size_t length,
    MinicC0Program *program,
    MinicDiagnostic *diagnostic)
{
    MinicParser parser;
    MinicExpression default_return;

    if (!minic_parser_initialize(
            &parser,
            path,
            source,
            length,
            program,
            diagnostic)) {
        return false;
    }
    if (!minic_parser_expect(
            &parser,
            MINIC_TOKEN_KW_INT,
            "expected keyword 'int'") ||
        !minic_parser_expect_main(&parser) ||
        !minic_parser_expect(
            &parser,
            MINIC_TOKEN_LPAREN,
            "expected '('")) {
        return false;
    }
    if (parser.current.kind != MINIC_TOKEN_RPAREN &&
        !minic_parser_expect(
            &parser,
            MINIC_TOKEN_KW_VOID,
            "expected keyword 'void'")) {
        return false;
    }
    if (!minic_parser_expect(
            &parser,
            MINIC_TOKEN_RPAREN,
            "expected ')'" ) ||
        !minic_parser_expect(
            &parser,
            MINIC_TOKEN_LBRACE,
            "expected '{'")) {
        return false;
    }

    if (parser.current.kind == MINIC_TOKEN_RBRACE) {
        default_return.kind = MINIC_EXPRESSION_INTEGER;
        default_return.span = parser.current.span;
        default_return.value.integer_value = 0;
        if (!minic_parser_add_expression(
                &parser,
                &default_return,
                &program->return_expression)) {
            return false;
        }
    } else {
        if (!minic_parser_expect(
                &parser,
                MINIC_TOKEN_KW_RETURN,
                "expected keyword 'return'") ||
            !minic_parser_parse_expression(
                &parser,
                &program->return_expression) ||
            !minic_parser_expect(
                &parser,
                MINIC_TOKEN_SEMICOLON,
                "expected ';'")) {
            return false;
        }
    }

    if (!minic_parser_expect(
            &parser,
            MINIC_TOKEN_RBRACE,
            "expected '}'")) {
        return false;
    }
    if (parser.current.kind != MINIC_TOKEN_EOF) {
        minic_parser_set_diagnostic(
            &parser,
            "unexpected input after main function");
        return false;
    }
    return true;
}

static bool minic_c0_literal_return_value(
    const MinicC0Program *program,
    int *return_value,
    MinicDiagnostic *diagnostic,
    const char *path)
{
    const MinicExpression *expression;

    expression = minic_c0_program_expression(
        program,
        program->return_expression);
    if (expression == NULL || expression->kind != MINIC_EXPRESSION_INTEGER) {
        if (diagnostic != NULL) {
            diagnostic->path = path;
            diagnostic->line = 1U;
            diagnostic->column = 1U;
            (void)snprintf(
                diagnostic->message,
                sizeof(diagnostic->message),
                "non-literal C0 expression requires the AST code generator");
        }
        return false;
    }
    *return_value = expression->value.integer_value;
    return true;
}

bool minic_parse_c0_translation_unit(
    const char *path,
    const char *source,
    size_t length,
    int *return_value,
    MinicDiagnostic *diagnostic)
{
    MinicC0Program program;
    bool success;

    minic_c0_program_initialize(&program);
    success = minic_parse_c0_program(
        path,
        source,
        length,
        &program,
        diagnostic);
    if (success) {
        success = minic_c0_literal_return_value(
            &program,
            return_value,
            diagnostic,
            path);
    }
    minic_c0_program_destroy(&program);
    return success;
}
