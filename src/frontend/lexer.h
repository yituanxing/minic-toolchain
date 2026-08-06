#ifndef MINIC_FRONTEND_LEXER_H
#define MINIC_FRONTEND_LEXER_H

#include "frontend/token.h"
#include "minic/compiler.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct MinicLexer {
    const char *path;
    const char *source;
    size_t length;
    size_t cursor;
    size_t line;
    size_t column;
} MinicLexer;

/*
 * The lexer borrows source and path storage for its complete lifetime.
 * Lexer 在整个生命周期内借用 source 与 path 的存储，不取得所有权。
 */
void minic_lexer_initialize(MinicLexer *lexer, const char *path, const char *source, size_t length);

bool minic_lexer_next(MinicLexer *lexer, MinicToken *token, MinicDiagnostic *diagnostic);

#endif
