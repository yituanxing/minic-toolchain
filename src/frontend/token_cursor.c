#include "frontend/token_cursor.h"

#include <string.h>

void minic_token_cursor_initialize(MinicTokenCursor *cursor,
                                   const MinicLexer *lexer,
                                   MinicToken current) {
    if (cursor == NULL || lexer == NULL) {
        return;
    }
    cursor->lexer = *lexer;
    cursor->current = current;
}

bool minic_token_cursor_advance(MinicTokenCursor *cursor, MinicDiagnostic *diagnostic) {
    return cursor != NULL && minic_lexer_next(&cursor->lexer, &cursor->current, diagnostic);
}

bool minic_token_cursor_text_is(const MinicTokenCursor *cursor, const char *text) {
    size_t length;
    size_t token_length;

    if (cursor == NULL || text == NULL || cursor->lexer.source == NULL ||
        cursor->current.span.end.offset < cursor->current.span.begin.offset) {
        return false;
    }
    length = strlen(text);
    token_length = cursor->current.span.end.offset - cursor->current.span.begin.offset;
    return token_length == length &&
           memcmp(cursor->lexer.source + cursor->current.span.begin.offset, text, length) == 0;
}
