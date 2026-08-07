#include "frontend/token.h"

#include <stdio.h>
#include <string.h>

static int expect_name(MinicTokenKind kind, const char *expected)
{
    const char *actual;

    actual = minic_token_kind_name(kind);
    if (strcmp(actual, expected) != 0) {
        (void)fprintf(
            stderr,
            "token name mismatch: expected='%s' actual='%s'\n",
            expected,
            actual);
        return 1;
    }
    return 0;
}

int main(void)
{
    MinicSourceSpan span;

    span.begin.offset = 4U;
    span.begin.line = 2U;
    span.begin.column = 3U;
    span.end.offset = 7U;
    span.end.line = 2U;
    span.end.column = 6U;

    if (span.begin.offset >= span.end.offset) {
        (void)fprintf(stderr, "source span must be non-empty in this fixture\n");
        return 1;
    }

    if (expect_name(MINIC_TOKEN_EOF, "end of file") != 0 ||
        expect_name(MINIC_TOKEN_IDENTIFIER, "identifier") != 0 ||
        expect_name(MINIC_TOKEN_INTEGER_CONSTANT, "integer constant") != 0 ||
        expect_name(MINIC_TOKEN_FLOATING_CONSTANT, "floating constant") != 0 ||
        expect_name(MINIC_TOKEN_STRING_LITERAL, "string literal") != 0 ||
        expect_name(MINIC_TOKEN_KW_CHAR, "char") != 0 ||
        expect_name(MINIC_TOKEN_KW_DOUBLE, "double") != 0 ||
        expect_name(MINIC_TOKEN_KW_FLOAT, "float") != 0 ||
        expect_name(MINIC_TOKEN_KW_LONG, "long") != 0 ||
        expect_name(MINIC_TOKEN_KW_SIGNED, "signed") != 0 ||
        expect_name(MINIC_TOKEN_KW_STRUCT, "struct") != 0 ||
        expect_name(MINIC_TOKEN_KW_CONST, "const") != 0 ||
        expect_name(MINIC_TOKEN_KW_RETURN, "return") != 0 ||
        expect_name(MINIC_TOKEN_KW_BREAK, "break") != 0 ||
        expect_name(MINIC_TOKEN_ELLIPSIS, "...") != 0 ||
        expect_name(MINIC_TOKEN_MINUS_MINUS, "--") != 0 ||
        expect_name(MINIC_TOKEN_ARROW, "->") != 0 ||
        expect_name(MINIC_TOKEN_AMPERSAND, "&") != 0 ||
        expect_name(MINIC_TOKEN_CARET, "^") != 0 ||
        expect_name(MINIC_TOKEN_CARET_EQUAL, "^=") != 0 ||
        expect_name(MINIC_TOKEN_LESS_LESS, "<<") != 0 ||
        expect_name(MINIC_TOKEN_GREATER_GREATER, ">>") != 0 ||
        expect_name(MINIC_TOKEN_SEMICOLON, ";") != 0 ||
        expect_name((MinicTokenKind)MINIC_TOKEN_KIND_COUNT, "unknown token") != 0) {
        return 1;
    }

    (void)printf("PASS frontend/token-model\n");
    return 0;
}
