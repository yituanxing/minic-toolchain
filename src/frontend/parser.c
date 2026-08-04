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
    MinicDiagnostic *diagnostic)
{
    parser->path = path;
    parser->source = source;
    parser->diagnostic = diagnostic;
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

static bool minic_parser_parse_integer(
    MinicParser *parser,
    int *value)
{
    size_t offset;
    size_t end;
    unsigned long result;

    if (parser->current.kind != MINIC_TOKEN_INTEGER_CONSTANT) {
        minic_parser_set_diagnostic(
            parser,
            "expected decimal integer constant");
        return false;
    }

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

    *value = (int)result;
    return minic_parser_advance(parser);
}

bool minic_parse_c0_translation_unit(
    const char *path,
    const char *source,
    size_t length,
    int *return_value,
    MinicDiagnostic *diagnostic)
{
    MinicParser parser;

    if (!minic_parser_initialize(
            &parser,
            path,
            source,
            length,
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
            "expected ')'") ||
        !minic_parser_expect(
            &parser,
            MINIC_TOKEN_LBRACE,
            "expected '{'")) {
        return false;
    }

    *return_value = 0;
    if (parser.current.kind != MINIC_TOKEN_RBRACE) {
        if (!minic_parser_expect(
                &parser,
                MINIC_TOKEN_KW_RETURN,
                "expected keyword 'return'") ||
            !minic_parser_parse_integer(&parser, return_value) ||
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
