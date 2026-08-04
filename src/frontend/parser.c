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
    bool saw_return;
} MinicParser;

static void minic_parser_set_diagnostic(
    MinicParser *parser,
    const char *format,
    ...)
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
    return minic_lexer_next(
        &parser->lexer,
        &parser->current,
        parser->diagnostic);
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
    parser->saw_return = false;
    minic_lexer_initialize(&parser->lexer, path, source, length);
    return minic_parser_advance(parser);
}

static size_t minic_span_length(MinicSourceSpan span)
{
    return span.end.offset - span.begin.offset;
}

static bool minic_parser_spans_equal(
    const MinicParser *parser,
    MinicSourceSpan left,
    MinicSourceSpan right)
{
    size_t left_length;
    size_t right_length;

    left_length = minic_span_length(left);
    right_length = minic_span_length(right);
    return left_length == right_length &&
           memcmp(
               parser->source + left.begin.offset,
               parser->source + right.begin.offset,
               left_length) == 0;
}

static bool minic_parser_token_text_equals(
    const MinicParser *parser,
    const char *expected)
{
    size_t token_length;
    size_t expected_length;

    token_length = minic_span_length(parser->current.span);
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

static bool minic_parser_add_statement(
    MinicParser *parser,
    const MinicStatement *statement)
{
    MinicStatementId statement_id;

    if (minic_c0_program_add_statement(
            parser->program,
            statement,
            &statement_id)) {
        return true;
    }
    minic_parser_set_diagnostic(
        parser,
        "out of memory while building statement list");
    return false;
}

static MinicLocalId minic_parser_find_local(
    const MinicParser *parser,
    MinicSourceSpan name_span)
{
    size_t index;

    for (index = 0U; index < parser->program->local_count; ++index) {
        if (minic_parser_spans_equal(
                parser,
                name_span,
                parser->program->locals[index].name_span)) {
            return index;
        }
    }
    return MINIC_LOCAL_INVALID;
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
    offset = expression.span.begin.offset;
    end = expression.span.end.offset;
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
    if (!minic_parser_add_expression(
            parser,
            &expression,
            expression_id)) {
        return false;
    }
    return minic_parser_advance(parser);
}

static bool minic_parser_parse_local_reference(
    MinicParser *parser,
    MinicExpressionId *expression_id)
{
    MinicExpression expression;
    MinicLocalId local_id;

    local_id = minic_parser_find_local(parser, parser->current.span);
    if (local_id == MINIC_LOCAL_INVALID) {
        minic_parser_set_diagnostic(parser, "use of undeclared local");
        return false;
    }

    expression.kind = MINIC_EXPRESSION_LOCAL;
    expression.span = parser->current.span;
    expression.value.local_id = local_id;
    if (!minic_parser_add_expression(
            parser,
            &expression,
            expression_id)) {
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
    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        return minic_parser_parse_local_reference(parser, expression_id);
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
    operand_expression = minic_c0_program_expression(
        parser->program,
        operand);
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
    return minic_parser_add_expression(
        parser,
        &expression,
        expression_id);
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
        left_expression = minic_c0_program_expression(
            parser->program,
            left);
        right_expression = minic_c0_program_expression(
            parser->program,
            right);
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
        if (!minic_parser_add_expression(
                parser,
                &expression,
                &left)) {
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
        left_expression = minic_c0_program_expression(
            parser->program,
            left);
        right_expression = minic_c0_program_expression(
            parser->program,
            right);
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
        if (!minic_parser_add_expression(
                parser,
                &expression,
                &left)) {
            return false;
        }
    }
    *expression_id = left;
    return true;
}

static bool minic_parser_parse_relational(
    MinicParser *parser,
    MinicExpressionId *expression_id)
{
    MinicExpressionId left;

    if (!minic_parser_parse_additive(parser, &left)) {
        return false;
    }
    while (parser->current.kind == MINIC_TOKEN_LESS ||
           parser->current.kind == MINIC_TOKEN_LESS_EQUAL ||
           parser->current.kind == MINIC_TOKEN_GREATER ||
           parser->current.kind == MINIC_TOKEN_GREATER_EQUAL) {
        MinicTokenKind operator_kind;
        MinicExpression expression;
        MinicExpressionId right;
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;

        operator_kind = parser->current.kind;
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_additive(parser, &right)) {
            return false;
        }
        left_expression = minic_c0_program_expression(parser->program, left);
        right_expression = minic_c0_program_expression(parser->program, right);
        if (left_expression == NULL || right_expression == NULL) {
            minic_parser_set_diagnostic(parser, "invalid comparison operand");
            return false;
        }
        expression.kind = MINIC_EXPRESSION_BINARY;
        expression.span.begin = left_expression->span.begin;
        expression.span.end = right_expression->span.end;
        expression.value.binary.left = left;
        expression.value.binary.right = right;
        switch (operator_kind) {
        case MINIC_TOKEN_LESS:
            expression.value.binary.operator_kind = MINIC_BINARY_LESS;
            break;
        case MINIC_TOKEN_LESS_EQUAL:
            expression.value.binary.operator_kind = MINIC_BINARY_LESS_EQUAL;
            break;
        case MINIC_TOKEN_GREATER:
            expression.value.binary.operator_kind = MINIC_BINARY_GREATER;
            break;
        case MINIC_TOKEN_GREATER_EQUAL:
            expression.value.binary.operator_kind = MINIC_BINARY_GREATER_EQUAL;
            break;
        default:
            return false;
        }
        if (!minic_parser_add_expression(parser, &expression, &left)) {
            return false;
        }
    }
    *expression_id = left;
    return true;
}

static bool minic_parser_parse_equality(
    MinicParser *parser,
    MinicExpressionId *expression_id)
{
    MinicExpressionId left;

    if (!minic_parser_parse_relational(parser, &left)) {
        return false;
    }
    while (parser->current.kind == MINIC_TOKEN_EQUAL_EQUAL ||
           parser->current.kind == MINIC_TOKEN_BANG_EQUAL) {
        MinicTokenKind operator_kind;
        MinicExpression expression;
        MinicExpressionId right;
        const MinicExpression *left_expression;
        const MinicExpression *right_expression;

        operator_kind = parser->current.kind;
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_relational(parser, &right)) {
            return false;
        }
        left_expression = minic_c0_program_expression(parser->program, left);
        right_expression = minic_c0_program_expression(parser->program, right);
        if (left_expression == NULL || right_expression == NULL) {
            minic_parser_set_diagnostic(parser, "invalid equality operand");
            return false;
        }
        expression.kind = MINIC_EXPRESSION_BINARY;
        expression.span.begin = left_expression->span.begin;
        expression.span.end = right_expression->span.end;
        expression.value.binary.operator_kind =
            operator_kind == MINIC_TOKEN_EQUAL_EQUAL
                ? MINIC_BINARY_EQUAL
                : MINIC_BINARY_NOT_EQUAL;
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
    return minic_parser_parse_equality(parser, expression_id);
}

static bool minic_parser_parse_declaration(MinicParser *parser)
{
    MinicLocal local;
    MinicLocalId local_id;

    if (!minic_parser_expect(
            parser,
            MINIC_TOKEN_KW_INT,
            "expected keyword 'int'")) {
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_set_diagnostic(parser, "expected local name");
        return false;
    }

    local.name_span = parser->current.span;
    if (minic_parser_find_local(parser, local.name_span) !=
        MINIC_LOCAL_INVALID) {
        minic_parser_set_diagnostic(parser, "duplicate local declaration");
        return false;
    }
    if (!minic_c0_program_add_local(
            parser->program,
            &local,
            &local_id)) {
        minic_parser_set_diagnostic(parser, "out of memory while adding local");
        return false;
    }
    if (!minic_parser_advance(parser)) {
        return false;
    }

    if (parser->current.kind == MINIC_TOKEN_EQUAL) {
        MinicStatement statement;
        const MinicExpression *initializer;

        statement.kind = MINIC_STATEMENT_ASSIGN;
        statement.span.begin = local.name_span.begin;
        statement.local_id = local_id;
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_expression(
                parser,
                &statement.expression)) {
            return false;
        }
        initializer = minic_c0_program_expression(
            parser->program,
            statement.expression);
        if (initializer == NULL) {
            minic_parser_set_diagnostic(parser, "invalid initializer");
            return false;
        }
        statement.span.end = initializer->span.end;
        if (!minic_parser_add_statement(parser, &statement)) {
            return false;
        }
    }

    return minic_parser_expect(
        parser,
        MINIC_TOKEN_SEMICOLON,
        "expected ';'");
}

static bool minic_parser_parse_assignment(MinicParser *parser)
{
    MinicSourceSpan name_span;
    MinicLocalId local_id;
    MinicStatement statement;
    const MinicExpression *value;

    name_span = parser->current.span;
    local_id = minic_parser_find_local(parser, name_span);
    if (local_id == MINIC_LOCAL_INVALID) {
        minic_parser_set_diagnostic(
            parser,
            "assignment to undeclared local");
        return false;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(
            parser,
            MINIC_TOKEN_EQUAL,
            "expected '='")) {
        return false;
    }

    statement.kind = MINIC_STATEMENT_ASSIGN;
    statement.span.begin = name_span.begin;
    statement.local_id = local_id;
    if (!minic_parser_parse_expression(
            parser,
            &statement.expression)) {
        return false;
    }
    value = minic_c0_program_expression(
        parser->program,
        statement.expression);
    if (value == NULL) {
        minic_parser_set_diagnostic(parser, "invalid assignment value");
        return false;
    }
    statement.span.end = value->span.end;
    if (!minic_parser_expect(
            parser,
            MINIC_TOKEN_SEMICOLON,
            "expected ';'")) {
        return false;
    }
    return minic_parser_add_statement(parser, &statement);
}

static bool minic_parser_parse_return(MinicParser *parser)
{
    MinicStatement statement;
    const MinicExpression *value;

    if (parser->saw_return) {
        minic_parser_set_diagnostic(
            parser,
            "multiple return statements require control-flow support");
        return false;
    }

    statement.kind = MINIC_STATEMENT_RETURN;
    statement.span.begin = parser->current.span.begin;
    statement.local_id = MINIC_LOCAL_INVALID;
    if (!minic_parser_advance(parser) ||
        !minic_parser_parse_expression(
            parser,
            &statement.expression)) {
        return false;
    }
    value = minic_c0_program_expression(
        parser->program,
        statement.expression);
    if (value == NULL) {
        minic_parser_set_diagnostic(parser, "invalid return value");
        return false;
    }
    statement.span.end = value->span.end;
    if (!minic_parser_expect(
            parser,
            MINIC_TOKEN_SEMICOLON,
            "expected ';'") ||
        !minic_parser_add_statement(parser, &statement)) {
        return false;
    }

    parser->program->return_expression = statement.expression;
    parser->saw_return = true;
    return true;
}

static bool minic_parser_add_default_return(MinicParser *parser)
{
    MinicExpression expression;
    MinicStatement statement;

    expression.kind = MINIC_EXPRESSION_INTEGER;
    expression.span = parser->current.span;
    expression.value.integer_value = 0;
    if (!minic_parser_add_expression(
            parser,
            &expression,
            &statement.expression)) {
        return false;
    }

    statement.kind = MINIC_STATEMENT_RETURN;
    statement.span = parser->current.span;
    statement.local_id = MINIC_LOCAL_INVALID;
    if (!minic_parser_add_statement(parser, &statement)) {
        return false;
    }
    parser->program->return_expression = statement.expression;
    return true;
}

bool minic_parse_c0_program(
    const char *path,
    const char *source,
    size_t length,
    MinicC0Program *program,
    MinicDiagnostic *diagnostic)
{
    MinicParser parser;

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
        parser.current.kind != MINIC_TOKEN_IDENTIFIER ||
        !minic_parser_token_text_equals(&parser, "main")) {
        if (parser.current.kind != MINIC_TOKEN_IDENTIFIER ||
            !minic_parser_token_text_equals(&parser, "main")) {
            minic_parser_set_diagnostic(
                &parser,
                "expected identifier 'main'");
        }
        return false;
    }
    if (!minic_parser_advance(&parser) ||
        !minic_parser_expect(
            &parser,
            MINIC_TOKEN_LPAREN,
            "expected '('") ) {
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

    while (parser.current.kind != MINIC_TOKEN_RBRACE) {
        if (parser.current.kind == MINIC_TOKEN_KW_INT) {
            if (!minic_parser_parse_declaration(&parser)) {
                return false;
            }
        } else if (parser.current.kind == MINIC_TOKEN_KW_RETURN) {
            if (!minic_parser_parse_return(&parser)) {
                return false;
            }
        } else if (parser.current.kind == MINIC_TOKEN_IDENTIFIER) {
            if (!minic_parser_parse_assignment(&parser)) {
                return false;
            }
        } else {
            minic_parser_set_diagnostic(
                &parser,
                "expected declaration, assignment, return, or '}'");
            return false;
        }
    }

    if (!parser.saw_return && !minic_parser_add_default_return(&parser)) {
        return false;
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
