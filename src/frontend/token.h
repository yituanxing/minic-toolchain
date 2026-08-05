#ifndef MINIC_FRONTEND_TOKEN_H
#define MINIC_FRONTEND_TOKEN_H

#include <stddef.h>

typedef struct MinicSourcePosition {
    size_t offset;
    size_t line;
    size_t column;
} MinicSourcePosition;

typedef struct MinicSourceSpan {
    MinicSourcePosition begin;
    MinicSourcePosition end;
} MinicSourceSpan;

typedef enum MinicTokenKind {
    MINIC_TOKEN_INVALID = 0,
    MINIC_TOKEN_EOF,
    MINIC_TOKEN_IDENTIFIER,
    MINIC_TOKEN_INTEGER_CONSTANT,
    MINIC_TOKEN_KW_INT,
    MINIC_TOKEN_KW_VOID,
    MINIC_TOKEN_KW_STRUCT,
    MINIC_TOKEN_KW_RETURN,
    MINIC_TOKEN_KW_IF,
    MINIC_TOKEN_KW_ELSE,
    MINIC_TOKEN_KW_WHILE,
    MINIC_TOKEN_LPAREN,
    MINIC_TOKEN_RPAREN,
    MINIC_TOKEN_LBRACE,
    MINIC_TOKEN_RBRACE,
    MINIC_TOKEN_SEMICOLON,
    MINIC_TOKEN_COMMA,
    MINIC_TOKEN_PLUS,
    MINIC_TOKEN_MINUS,
    MINIC_TOKEN_STAR,
    MINIC_TOKEN_AMPERSAND,
    MINIC_TOKEN_SLASH,
    MINIC_TOKEN_PERCENT,
    MINIC_TOKEN_EQUAL,
    MINIC_TOKEN_EQUAL_EQUAL,
    MINIC_TOKEN_BANG,
    MINIC_TOKEN_BANG_EQUAL,
    MINIC_TOKEN_LESS,
    MINIC_TOKEN_LESS_EQUAL,
    MINIC_TOKEN_GREATER,
    MINIC_TOKEN_GREATER_EQUAL,
    MINIC_TOKEN_LBRACKET,
    MINIC_TOKEN_RBRACKET,
    MINIC_TOKEN_KIND_COUNT
} MinicTokenKind;

typedef struct MinicToken {
    MinicTokenKind kind;
    MinicSourceSpan span;
} MinicToken;

const char *minic_token_kind_name(MinicTokenKind kind);

#endif
