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
    MinicBlockId current_block;
} MinicParser;

static void parser_error(MinicParser *parser, const char *format, ...)
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

static bool parser_advance(MinicParser *parser)
{
    return minic_lexer_next(&parser->lexer, &parser->current, parser->diagnostic);
}

static bool parser_expect(
    MinicParser *parser,
    MinicTokenKind kind,
    const char *message)
{
    if (parser->current.kind != kind) {
        parser_error(parser, "%s", message);
        return false;
    }
    return parser_advance(parser);
}

static size_t span_length(MinicSourceSpan span)
{
    return span.end.offset - span.begin.offset;
}

static bool span_equals(
    const MinicParser *parser,
    MinicSourceSpan left,
    MinicSourceSpan right)
{
    size_t left_length;
    size_t right_length;

    left_length = span_length(left);
    right_length = span_length(right);
    return left_length == right_length &&
           memcmp(
               parser->source + left.begin.offset,
               parser->source + right.begin.offset,
               left_length) == 0;
}

static bool current_text_equals(
    const MinicParser *parser,
    const char *expected)
{
    size_t length;

    length = span_length(parser->current.span);
    return length == strlen(expected) &&
           memcmp(
               parser->source + parser->current.span.begin.offset,
               expected,
               length) == 0;
}

static bool add_expression(
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
    parser_error(parser, "out of memory while building expression tree");
    return false;
}

static bool add_statement(
    MinicParser *parser,
    const MinicStatement *statement)
{
    MinicStatementId statement_id;

    if (minic_c0_program_add_statement(
            parser->program,
            statement,
            &statement_id) &&
        minic_c0_block_add_statement(
            parser->program,
            parser->current_block,
            statement_id)) {
        return true;
    }
    parser_error(parser, "out of memory while building statement list");
    return false;
}

static MinicLocalId find_local(
    const MinicParser *parser,
    MinicSourceSpan name_span)
{
    size_t index;

    for (index = 0U; index < parser->program->local_count; ++index) {
        if (span_equals(
                parser,
                name_span,
                parser->program->locals[index].name_span)) {
            return index;
        }
    }
    return MINIC_LOCAL_INVALID;
}

static bool parse_expression(
    MinicParser *parser,
    MinicExpressionId *expression_id,
    unsigned int minimum_precedence);

static bool parse_integer(
    MinicParser *parser,
    MinicExpressionId *expression_id)
{
    MinicExpression expression;
    size_t offset;
    unsigned long value;

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_INTEGER;
    expression.span = parser->current.span;
    value = 0UL;
    for (offset = expression.span.begin.offset;
         offset < expression.span.end.offset;
         ++offset) {
        unsigned long digit;

        digit = (unsigned long)(unsigned int)(parser->source[offset] - '0');
        if (value > ((unsigned long)INT_MAX - digit) / 10UL) {
            parser_error(parser, "integer constant exceeds C0 int range");
            return false;
        }
        value = value * 10UL + digit;
    }
    expression.value.integer_value = (int)value;
    return add_expression(parser, &expression, expression_id) &&
           parser_advance(parser);
}

static bool parse_primary(
    MinicParser *parser,
    MinicExpressionId *expression_id)
{
    MinicExpression expression;
    MinicLocalId local_id;

    if (parser->current.kind == MINIC_TOKEN_INTEGER_CONSTANT) {
        return parse_integer(parser, expression_id);
    }
    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        local_id = find_local(parser, parser->current.span);
        if (local_id == MINIC_LOCAL_INVALID) {
            parser_error(parser, "use of undeclared local");
            return false;
        }
        (void)memset(&expression, 0, sizeof(expression));
        expression.kind = MINIC_EXPRESSION_LOCAL;
        expression.span = parser->current.span;
        expression.value.local_id = local_id;
        return add_expression(parser, &expression, expression_id) &&
               parser_advance(parser);
    }
    if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        return parser_advance(parser) &&
               parse_expression(parser, expression_id, 0U) &&
               parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')'");
    }
    parser_error(parser, "expected expression");
    return false;
}

static bool parse_unary(
    MinicParser *parser,
    MinicExpressionId *expression_id)
{
    MinicToken operator_token;
    MinicExpression expression;
    MinicExpressionId operand;
    const MinicExpression *operand_expression;

    if (parser->current.kind != MINIC_TOKEN_PLUS &&
        parser->current.kind != MINIC_TOKEN_MINUS &&
        parser->current.kind != MINIC_TOKEN_BANG) {
        return parse_primary(parser, expression_id);
    }

    operator_token = parser->current;
    if (!parser_advance(parser) || !parse_unary(parser, &operand)) {
        return false;
    }
    operand_expression = minic_c0_program_expression(parser->program, operand);
    if (operand_expression == NULL) {
        parser_error(parser, "invalid unary operand");
        return false;
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_UNARY;
    expression.span.begin = operator_token.span.begin;
    expression.span.end = operand_expression->span.end;
    expression.value.unary.operand = operand;
    if (operator_token.kind == MINIC_TOKEN_PLUS) {
        expression.value.unary.operator_kind = MINIC_UNARY_PLUS;
    } else if (operator_token.kind == MINIC_TOKEN_MINUS) {
        expression.value.unary.operator_kind = MINIC_UNARY_NEGATE;
    } else {
        expression.value.unary.operator_kind = MINIC_UNARY_LOGICAL_NOT;
    }
    return add_expression(parser, &expression, expression_id);
}

static unsigned int binary_precedence(MinicTokenKind kind)
{
    switch (kind) {
    case MINIC_TOKEN_STAR:
    case MINIC_TOKEN_SLASH:
    case MINIC_TOKEN_PERCENT:
        return 50U;
    case MINIC_TOKEN_PLUS:
    case MINIC_TOKEN_MINUS:
        return 40U;
    case MINIC_TOKEN_LESS:
    case MINIC_TOKEN_LESS_EQUAL:
    case MINIC_TOKEN_GREATER:
    case MINIC_TOKEN_GREATER_EQUAL:
        return 30U;
    case MINIC_TOKEN_EQUAL_EQUAL:
    case MINIC_TOKEN_BANG_EQUAL:
        return 20U;
    default:
        return 0U;
    }
}

static MinicBinaryOperator binary_operator(MinicTokenKind kind)
{
    switch (kind) {
    case MINIC_TOKEN_PLUS:
        return MINIC_BINARY_ADD;
    case MINIC_TOKEN_MINUS:
        return MINIC_BINARY_SUBTRACT;
    case MINIC_TOKEN_STAR:
        return MINIC_BINARY_MULTIPLY;
    case MINIC_TOKEN_SLASH:
        return MINIC_BINARY_DIVIDE;
    case MINIC_TOKEN_PERCENT:
        return MINIC_BINARY_REMAINDER;
    case MINIC_TOKEN_EQUAL_EQUAL:
        return MINIC_BINARY_EQUAL;
    case MINIC_TOKEN_BANG_EQUAL:
        return MINIC_BINARY_NOT_EQUAL;
    case MINIC_TOKEN_LESS:
        return MINIC_BINARY_LESS;
    case MINIC_TOKEN_LESS_EQUAL:
        return MINIC_BINARY_LESS_EQUAL;
    case MINIC_TOKEN_GREATER:
        return MINIC_BINARY_GREATER;
    case MINIC_TOKEN_GREATER_EQUAL:
        return MINIC_BINARY_GREATER_EQUAL;
    default:
        return MINIC_BINARY_ADD;
    }
}

static bool parse_expression(
    MinicParser *parser,
    MinicExpressionId *expression_id,
    unsigned int minimum_precedence)
{
    MinicExpressionId left;

    if (!parse_unary(parser, &left)) {
        return false;
    }
    for (;;) {
        MinicTokenKind token_kind;
        unsigned int precedence;
        MinicExpressionId right;
        MinicExpression expression;
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;

        token_kind = parser->current.kind;
        precedence = binary_precedence(token_kind);
        if (precedence == 0U || precedence < minimum_precedence) {
            break;
        }
        if (!parser_advance(parser) ||
            !parse_expression(parser, &right, precedence + 1U)) {
            return false;
        }
        left_expression = minic_c0_program_expression(parser->program, left);
        right_expression = minic_c0_program_expression(parser->program, right);
        if (left_expression == NULL || right_expression == NULL) {
            parser_error(parser, "invalid binary operand");
            return false;
        }

        (void)memset(&expression, 0, sizeof(expression));
        expression.kind = MINIC_EXPRESSION_BINARY;
        expression.span.begin = left_expression->span.begin;
        expression.span.end = right_expression->span.end;
        expression.value.binary.operator_kind = binary_operator(token_kind);
        expression.value.binary.left = left;
        expression.value.binary.right = right;
        if (!add_expression(parser, &expression, &left)) {
            return false;
        }
    }
    *expression_id = left;
    return true;
}

static bool parse_declaration(MinicParser *parser)
{
    MinicLocal local;
    MinicLocalId local_id;

    if (!parser_expect(parser, MINIC_TOKEN_KW_INT, "expected keyword 'int'") ||
        parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
            parser_error(parser, "expected local name");
        }
        return false;
    }

    local.name_span = parser->current.span;
    if (find_local(parser, local.name_span) != MINIC_LOCAL_INVALID) {
        parser_error(parser, "duplicate local declaration");
        return false;
    }
    if (!minic_c0_program_add_local(parser->program, &local, &local_id)) {
        parser_error(parser, "out of memory while adding local");
        return false;
    }
    if (!parser_advance(parser)) {
        return false;
    }

    if (parser->current.kind == MINIC_TOKEN_EQUAL) {
        MinicStatement statement;

        (void)memset(&statement, 0, sizeof(statement));
        statement.kind = MINIC_STATEMENT_ASSIGN;
        statement.span.begin = local.name_span.begin;
        statement.local_id = local_id;
        if (!parser_advance(parser) ||
            !parse_expression(parser, &statement.expression, 0U)) {
            return false;
        }
        statement.span.end =
            minic_c0_program_expression(parser->program, statement.expression)->span.end;
        if (!add_statement(parser, &statement)) {
            return false;
        }
    }
    return parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';'");
}

static bool parse_assignment(MinicParser *parser)
{
    MinicStatement statement;
    MinicSourceSpan name_span;

    (void)memset(&statement, 0, sizeof(statement));
    name_span = parser->current.span;
    statement.kind = MINIC_STATEMENT_ASSIGN;
    statement.span.begin = name_span.begin;
    statement.local_id = find_local(parser, name_span);
    if (statement.local_id == MINIC_LOCAL_INVALID) {
        parser_error(parser, "assignment to undeclared local");
        return false;
    }
    if (!parser_advance(parser) ||
        !parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '='") ||
        !parse_expression(parser, &statement.expression, 0U)) {
        return false;
    }
    statement.span.end =
        minic_c0_program_expression(parser->program, statement.expression)->span.end;
    return parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';'") &&
           add_statement(parser, &statement);
}

static bool parse_return(MinicParser *parser)
{
    MinicStatement statement;

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_RETURN;
    statement.span.begin = parser->current.span.begin;
    statement.local_id = MINIC_LOCAL_INVALID;
    if (!parser_advance(parser) ||
        !parse_expression(parser, &statement.expression, 0U)) {
        return false;
    }
    statement.span.end =
        minic_c0_program_expression(parser->program, statement.expression)->span.end;
    if (!parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';'") ||
        !add_statement(parser, &statement)) {
        return false;
    }
    parser->program->return_expression = statement.expression;
    return true;
}

static bool add_default_return(MinicParser *parser)
{
    MinicExpression expression;
    MinicStatement statement;

    (void)memset(&expression, 0, sizeof(expression));
    (void)memset(&statement, 0, sizeof(statement));
    expression.kind = MINIC_EXPRESSION_INTEGER;
    expression.span = parser->current.span;
    expression.value.integer_value = 0;
    if (!add_expression(parser, &expression, &statement.expression)) {
        return false;
    }
    statement.kind = MINIC_STATEMENT_RETURN;
    statement.span = parser->current.span;
    statement.local_id = MINIC_LOCAL_INVALID;
    parser->program->return_expression = statement.expression;
    return add_statement(parser, &statement);
}

static bool parse_statement(MinicParser *parser)
{
    if (parser->current.kind == MINIC_TOKEN_KW_INT) {
        return parse_declaration(parser);
    }
    if (parser->current.kind == MINIC_TOKEN_KW_RETURN) {
        return parse_return(parser);
    }
    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        return parse_assignment(parser);
    }
    parser_error(parser, "expected declaration, assignment, return, or '}'");
    return false;
}

bool minic_parse_c0_program(
    const char *path,
    const char *source,
    size_t length,
    MinicC0Program *program,
    MinicDiagnostic *diagnostic)
{
    MinicParser parser;

    parser.path = path;
    parser.source = source;
    parser.diagnostic = diagnostic;
    parser.program = program;
    parser.current_block = MINIC_BLOCK_INVALID;
    minic_lexer_initialize(&parser.lexer, path, source, length);
    if (!parser_advance(&parser) ||
        !parser_expect(&parser, MINIC_TOKEN_KW_INT, "expected keyword 'int'") ||
        parser.current.kind != MINIC_TOKEN_IDENTIFIER ||
        !current_text_equals(&parser, "main")) {
        if (parser.current.kind != MINIC_TOKEN_IDENTIFIER ||
            !current_text_equals(&parser, "main")) {
            parser_error(&parser, "expected identifier 'main'");
        }
        return false;
    }
    if (!parser_advance(&parser) ||
        !parser_expect(&parser, MINIC_TOKEN_LPAREN, "expected '('") ) {
        return false;
    }
    if (parser.current.kind != MINIC_TOKEN_RPAREN &&
        !parser_expect(&parser, MINIC_TOKEN_KW_VOID, "expected keyword 'void'")) {
        return false;
    }
    if (!parser_expect(&parser, MINIC_TOKEN_RPAREN, "expected ')'") ||
        !parser_expect(&parser, MINIC_TOKEN_LBRACE, "expected '{'") ||
        !minic_c0_program_add_block(program, &program->body_block)) {
        if (program->body_block == MINIC_BLOCK_INVALID) {
            parser_error(&parser, "out of memory while adding function body");
        }
        return false;
    }
    parser.current_block = program->body_block;

    while (parser.current.kind != MINIC_TOKEN_RBRACE) {
        if (!parse_statement(&parser)) {
            return false;
        }
    }
    if (!add_default_return(&parser) ||
        !parser_expect(&parser, MINIC_TOKEN_RBRACE, "expected '}'")) {
        return false;
    }
    if (parser.current.kind != MINIC_TOKEN_EOF) {
        parser_error(&parser, "unexpected input after main function");
        return false;
    }
    return true;
}
