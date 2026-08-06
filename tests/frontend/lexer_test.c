#include "frontend/lexer.h"

#include <stdio.h>
#include <string.h>

static int expect_token(
    MinicLexer *lexer,
    MinicTokenKind expected_kind,
    size_t expected_line,
    size_t expected_column)
{
    MinicDiagnostic diagnostic;
    MinicToken token;

    diagnostic.message[0] = '\0';
    if (!minic_lexer_next(lexer, &token, &diagnostic)) {
        (void)fprintf(
            stderr,
            "unexpected lexer failure at %zu:%zu: %s\n",
            diagnostic.line,
            diagnostic.column,
            diagnostic.message);
        return 1;
    }
    if (token.kind != expected_kind ||
        token.span.begin.line != expected_line ||
        token.span.begin.column != expected_column) {
        (void)fprintf(
            stderr,
            "token mismatch: expected=%s@%zu:%zu actual=%s@%zu:%zu\n",
            minic_token_kind_name(expected_kind),
            expected_line,
            expected_column,
            minic_token_kind_name(token.kind),
            token.span.begin.line,
            token.span.begin.column);
        return 1;
    }
    if (token.kind != MINIC_TOKEN_EOF &&
        token.span.begin.offset >= token.span.end.offset) {
        (void)fprintf(stderr, "non-EOF token has an empty source span\n");
        return 1;
    }
    return 0;
}

static int test_c0_sequence(void)
{
    static const char source[] =
        "int main(void) {\n"
        "  return 42;\n"
        "}\n";
    static const struct {
        MinicTokenKind kind;
        size_t line;
        size_t column;
    } expected[] = {
        {MINIC_TOKEN_KW_INT, 1U, 1U},
        {MINIC_TOKEN_IDENTIFIER, 1U, 5U},
        {MINIC_TOKEN_LPAREN, 1U, 9U},
        {MINIC_TOKEN_KW_VOID, 1U, 10U},
        {MINIC_TOKEN_RPAREN, 1U, 14U},
        {MINIC_TOKEN_LBRACE, 1U, 16U},
        {MINIC_TOKEN_KW_RETURN, 2U, 3U},
        {MINIC_TOKEN_INTEGER_CONSTANT, 2U, 10U},
        {MINIC_TOKEN_SEMICOLON, 2U, 12U},
        {MINIC_TOKEN_RBRACE, 3U, 1U},
        {MINIC_TOKEN_EOF, 4U, 1U}
    };
    MinicLexer lexer;
    size_t index;

    minic_lexer_initialize(&lexer, "sequence.c", source, sizeof(source) - 1U);
    for (index = 0U; index < sizeof(expected) / sizeof(expected[0]); ++index) {
        if (expect_token(
                &lexer,
                expected[index].kind,
                expected[index].line,
                expected[index].column) != 0) {
            return 1;
        }
    }
    return 0;
}

static int test_operator_sequence(void)
{
    static const char source[] = "+ ++ - -- -> * & ^ ^= / % = [ ]";
    static const struct {
        MinicTokenKind kind;
        size_t column;
    } expected[] = {
        {MINIC_TOKEN_PLUS, 1U},
        {MINIC_TOKEN_PLUS_PLUS, 3U},
        {MINIC_TOKEN_MINUS, 6U},
        {MINIC_TOKEN_MINUS_MINUS, 8U},
        {MINIC_TOKEN_ARROW, 11U},
        {MINIC_TOKEN_STAR, 14U},
        {MINIC_TOKEN_AMPERSAND, 16U},
        {MINIC_TOKEN_CARET, 18U},
        {MINIC_TOKEN_CARET_EQUAL, 20U},
        {MINIC_TOKEN_SLASH, 23U},
        {MINIC_TOKEN_PERCENT, 25U},
        {MINIC_TOKEN_EQUAL, 27U},
        {MINIC_TOKEN_LBRACKET, 29U},
        {MINIC_TOKEN_RBRACKET, 31U},
        {MINIC_TOKEN_EOF, 32U}
    };
    MinicLexer lexer;
    size_t index;

    minic_lexer_initialize(&lexer, "operators.c", source, sizeof(source) - 1U);
    for (index = 0U; index < sizeof(expected) / sizeof(expected[0]); ++index) {
        if (expect_token(
                &lexer,
                expected[index].kind,
                1U,
                expected[index].column) != 0) {
            return 1;
        }
    }
    return 0;
}

static int test_comparison_operators(void)
{
    static const char source[] = "= == ! != < << <= > >> >=";
    static const struct {
        MinicTokenKind kind;
        size_t column;
    } expected[] = {
        {MINIC_TOKEN_EQUAL, 1U},
        {MINIC_TOKEN_EQUAL_EQUAL, 3U},
        {MINIC_TOKEN_BANG, 6U},
        {MINIC_TOKEN_BANG_EQUAL, 8U},
        {MINIC_TOKEN_LESS, 11U},
        {MINIC_TOKEN_LESS_LESS, 13U},
        {MINIC_TOKEN_LESS_EQUAL, 16U},
        {MINIC_TOKEN_GREATER, 19U},
        {MINIC_TOKEN_GREATER_GREATER, 21U},
        {MINIC_TOKEN_GREATER_EQUAL, 24U},
        {MINIC_TOKEN_EOF, 26U}
    };
    MinicLexer lexer;
    size_t index;

    minic_lexer_initialize(
        &lexer,
        "comparisons.c",
        source,
        sizeof(source) - 1U);
    for (index = 0U; index < sizeof(expected) / sizeof(expected[0]); ++index) {
        if (expect_token(
                &lexer,
                expected[index].kind,
                1U,
                expected[index].column) != 0) {
            return 1;
        }
    }
    return 0;
}

static int test_control_keyword_boundaries(void)
{
    static const char source[] = "if else if_value elsewhere";
    MinicLexer lexer;

    minic_lexer_initialize(&lexer, "control.c", source, sizeof(source) - 1U);
    if (expect_token(&lexer, MINIC_TOKEN_KW_IF, 1U, 1U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_KW_ELSE, 1U, 4U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_IDENTIFIER, 1U, 9U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_IDENTIFIER, 1U, 18U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_EOF, 1U, 27U) != 0) {
        return 1;
    }
    return 0;
}

static int test_for_keyword_boundaries(void)
{
    static const char source[] = "for format for_value";
    MinicLexer lexer;

    minic_lexer_initialize(&lexer, "for.c", source, sizeof(source) - 1U);
    if (expect_token(&lexer, MINIC_TOKEN_KW_FOR, 1U, 1U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_IDENTIFIER, 1U, 5U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_IDENTIFIER, 1U, 12U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_EOF, 1U, 21U) != 0) {
        return 1;
    }
    return 0;
}

static int test_break_keyword_boundaries(void)
{
    static const char source[] = "break breakfast break_value";
    MinicLexer lexer;

    minic_lexer_initialize(&lexer, "break.c", source, sizeof(source) - 1U);
    if (expect_token(&lexer, MINIC_TOKEN_KW_BREAK, 1U, 1U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_IDENTIFIER, 1U, 7U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_IDENTIFIER, 1U, 17U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_EOF, 1U, 28U) != 0) {
        return 1;
    }
    return 0;
}

static int test_struct_keyword_boundaries(void)
{
    static const char source[] = "struct AES_ctx structure struct_value";
    MinicLexer lexer;

    minic_lexer_initialize(&lexer, "struct.c", source, sizeof(source) - 1U);
    if (expect_token(&lexer, MINIC_TOKEN_KW_STRUCT, 1U, 1U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_IDENTIFIER, 1U, 8U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_IDENTIFIER, 1U, 16U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_IDENTIFIER, 1U, 26U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_EOF, 1U, 38U) != 0) {
        return 1;
    }
    return 0;
}

static int test_const_keyword_boundaries(void)
{
    static const char source[] = "const constant const_value";
    MinicLexer lexer;

    minic_lexer_initialize(&lexer, "const.c", source, sizeof(source) - 1U);
    if (expect_token(&lexer, MINIC_TOKEN_KW_CONST, 1U, 1U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_IDENTIFIER, 1U, 7U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_IDENTIFIER, 1U, 16U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_EOF, 1U, 27U) != 0) {
        return 1;
    }
    return 0;
}

static int test_unsigned_keyword_boundaries(void)
{
    static const char source[] = "unsigned unsigned_value unsignedness";
    MinicLexer lexer;

    minic_lexer_initialize(&lexer, "unsigned.c", source, sizeof(source) - 1U);
    if (expect_token(&lexer, MINIC_TOKEN_KW_UNSIGNED, 1U, 1U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_IDENTIFIER, 1U, 10U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_IDENTIFIER, 1U, 25U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_EOF, 1U, 37U) != 0) {
        return 1;
    }
    return 0;
}

static int test_signed_keyword_boundaries(void)
{
    static const char source[] = "signed signed_value signedness";
    MinicLexer lexer;

    minic_lexer_initialize(&lexer, "signed.c", source, sizeof(source) - 1U);
    if (expect_token(&lexer, MINIC_TOKEN_KW_SIGNED, 1U, 1U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_IDENTIFIER, 1U, 8U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_IDENTIFIER, 1U, 21U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_EOF, 1U, 31U) != 0) {
        return 1;
    }
    return 0;
}

static int test_long_keyword_boundaries(void)
{
    static const char source[] = "long longer long_value";
    MinicLexer lexer;

    minic_lexer_initialize(&lexer, "long.c", source, sizeof(source) - 1U);
    if (expect_token(&lexer, MINIC_TOKEN_KW_LONG, 1U, 1U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_IDENTIFIER, 1U, 6U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_IDENTIFIER, 1U, 13U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_EOF, 1U, 23U) != 0) {
        return 1;
    }
    return 0;
}

static int test_char_keyword_boundaries(void)
{
    static const char source[] = "char char_value character";
    MinicLexer lexer;

    minic_lexer_initialize(&lexer, "char.c", source, sizeof(source) - 1U);
    if (expect_token(&lexer, MINIC_TOKEN_KW_CHAR, 1U, 1U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_IDENTIFIER, 1U, 6U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_IDENTIFIER, 1U, 17U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_EOF, 1U, 26U) != 0) {
        return 1;
    }
    return 0;
}

static int test_keyword_boundaries(void)
{
    static const char source[] = "integer return_value voided";
    MinicLexer lexer;

    minic_lexer_initialize(&lexer, "identifiers.c", source, sizeof(source) - 1U);
    if (expect_token(&lexer, MINIC_TOKEN_IDENTIFIER, 1U, 1U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_IDENTIFIER, 1U, 9U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_IDENTIFIER, 1U, 22U) != 0 ||
        expect_token(&lexer, MINIC_TOKEN_EOF, 1U, 28U) != 0) {
        return 1;
    }
    return 0;
}

static int test_invalid_character(void)
{
    static const char source[] = "\n  @";
    MinicDiagnostic diagnostic;
    MinicLexer lexer;
    MinicToken token;

    minic_lexer_initialize(&lexer, "invalid.c", source, sizeof(source) - 1U);
    diagnostic.message[0] = '\0';
    if (minic_lexer_next(&lexer, &token, &diagnostic)) {
        (void)fprintf(stderr, "invalid character unexpectedly tokenized\n");
        return 1;
    }
    if (token.kind != MINIC_TOKEN_INVALID ||
        diagnostic.line != 2U || diagnostic.column != 3U ||
        strcmp(diagnostic.message, "unexpected character '@'") != 0) {
        (void)fprintf(
            stderr,
            "invalid diagnostic mismatch: %zu:%zu: %s\n",
            diagnostic.line,
            diagnostic.column,
            diagnostic.message);
        return 1;
    }
    return 0;
}

int main(void)
{
    if (test_c0_sequence() != 0 ||
        test_operator_sequence() != 0 ||
        test_comparison_operators() != 0 ||
        test_control_keyword_boundaries() != 0 ||
        test_for_keyword_boundaries() != 0 ||
        test_break_keyword_boundaries() != 0 ||
        test_struct_keyword_boundaries() != 0 ||
        test_const_keyword_boundaries() != 0 ||
        test_unsigned_keyword_boundaries() != 0 ||
        test_signed_keyword_boundaries() != 0 ||
        test_long_keyword_boundaries() != 0 ||
        test_char_keyword_boundaries() != 0 ||
        test_keyword_boundaries() != 0 ||
        test_invalid_character() != 0) {
        return 1;
    }

    (void)printf("PASS frontend/lexer\n");
    return 0;
}