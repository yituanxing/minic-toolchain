#ifndef MINILD_LINKER_SCRIPT_INTERNAL_H
#define MINILD_LINKER_SCRIPT_INTERNAL_H

#include "linker_script.h"

typedef enum ScriptTokenKind {
    TOKEN_EOF = 0,
    TOKEN_IDENTIFIER,
    TOKEN_NUMBER,
    TOKEN_STRING,
    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_COLON,
    TOKEN_SEMICOLON,
    TOKEN_EQUAL,
    TOKEN_STAR,
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_SLASH,
    TOKEN_PERCENT,
    TOKEN_AMP,
    TOKEN_CARET,
    TOKEN_PIPE,
    TOKEN_TILDE,
    TOKEN_SHIFT_LEFT,
    TOKEN_SHIFT_RIGHT,
    TOKEN_COMMA,
    TOKEN_PLUS_EQUAL,
    TOKEN_LESS,
    TOKEN_LESS_EQUAL,
    TOKEN_GREATER,
    TOKEN_GREATER_EQUAL,
    TOKEN_EQUAL_EQUAL,
    TOKEN_NOT_EQUAL,
    TOKEN_LOGICAL_NOT,
    TOKEN_LOGICAL_AND,
    TOKEN_LOGICAL_OR
} ScriptTokenKind;

typedef struct ScriptToken {
    ScriptTokenKind kind;
    const char *begin;
    size_t length;
    uint64_t number;
    size_t line;
    size_t column;
} ScriptToken;

typedef struct ScriptParser {
    const char *path;
    char *source;
    size_t source_size;
    size_t offset;
    size_t line;
    size_t column;
    ScriptToken token;
    MiniLdScript *script;
    FILE *diagnostics;
} ScriptParser;

char *minild_script_strdup_range(const char *text, size_t length);
bool minild_script_parser_error(ScriptParser *parser, const char *message);
bool minild_script_read_entire_file(const char *path, char **data_out, size_t *size_out);
bool minild_script_parser_next(ScriptParser *parser);
bool minild_script_token_is(const ScriptParser *parser, const char *text);
bool minild_script_consume(ScriptParser *parser, ScriptTokenKind kind);
bool minild_script_expect(ScriptParser *parser, ScriptTokenKind kind, const char *message);
bool minild_script_parse_expression(ScriptParser *parser, MiniLdScriptExprId *expression_out);
bool minild_script_append_expression(MiniLdScript *script,
                                    MiniLdScriptExpr expression,
                                    MiniLdScriptExprId *id_out);

#endif
