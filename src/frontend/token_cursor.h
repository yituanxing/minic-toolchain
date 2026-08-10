#ifndef MINIC_FRONTEND_TOKEN_CURSOR_H
#define MINIC_FRONTEND_TOKEN_CURSOR_H

#include "frontend/lexer.h"
#include "frontend/token.h"
#include "minic/compiler.h"

#include <stdbool.h>

/*
 * A TokenCursor is a semantic-free lookahead snapshot.  It owns no source or
 * compiler state: copying/advancing it may change only the copied lexer/token.
 * TokenCursor 是纯词法前瞻快照，不持有 Program/Sema 状态；推进它只能修改自身。
 */
typedef struct MinicTokenCursor {
    MinicLexer lexer;
    MinicToken current;
} MinicTokenCursor;

void minic_token_cursor_initialize(MinicTokenCursor *cursor,
                                   const MinicLexer *lexer,
                                   MinicToken current);
bool minic_token_cursor_advance(MinicTokenCursor *cursor, MinicDiagnostic *diagnostic);
bool minic_token_cursor_text_is(const MinicTokenCursor *cursor, const char *text);

#endif
