#include "frontend/token_cursor.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *message) {
    (void)fprintf(stderr, "FAIL frontend/token-cursor: %s\n", message);
    return 1;
}

int main(void) {
    static const char source[] = "first second";
    MinicDiagnostic diagnostic;
    MinicLexer lexer;
    MinicToken first;
    MinicTokenCursor cursor;
    size_t saved_cursor;
    size_t saved_line;
    size_t saved_column;

    (void)memset(&diagnostic, 0, sizeof(diagnostic));
    minic_lexer_initialize(&lexer, "token_cursor_test.c", source, sizeof(source) - 1U);
    if (!minic_lexer_next(&lexer, &first, &diagnostic) ||
        first.kind != MINIC_TOKEN_IDENTIFIER) {
        return fail("cannot establish first token");
    }

    saved_cursor = lexer.cursor;
    saved_line = lexer.line;
    saved_column = lexer.column;
    minic_token_cursor_initialize(&cursor, &lexer, first);
    if (!minic_token_cursor_text_is(&cursor, "first") ||
        !minic_token_cursor_advance(&cursor, &diagnostic) ||
        cursor.current.kind != MINIC_TOKEN_IDENTIFIER ||
        !minic_token_cursor_text_is(&cursor, "second")) {
        return fail("lookahead cursor did not advance independently");
    }
    if (lexer.cursor != saved_cursor || lexer.line != saved_line || lexer.column != saved_column ||
        first.kind != MINIC_TOKEN_IDENTIFIER) {
        return fail("lookahead mutated original lexer/token state");
    }
    if (!minic_token_cursor_advance(&cursor, &diagnostic) ||
        cursor.current.kind != MINIC_TOKEN_EOF) {
        return fail("cursor did not reach EOF");
    }

    (void)printf("PASS frontend/token-cursor semantic-state=absent lexer-snapshot=isolated\n");
    return 0;
}
