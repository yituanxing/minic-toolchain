#ifndef MINIC_FRONTEND_TOKEN_H
#define MINIC_FRONTEND_TOKEN_H

#include <stddef.h>

typedef struct MinicSourcePosition {
    size_t offset;
    size_t line;
    size_t column;
} MinicSourcePosition;

/*
 * Source spans use an inclusive begin and exclusive end position.
 * SourceSpan 使用包含起点、不包含终点的半开区间。
 */
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
    MINIC_TOKEN_KW_RETURN,
    MINIC_TOKEN_LPAREN,
    MINIC_TOKEN_RPAREN,
    MINIC_TOKEN_LBRACE,
    MINIC_TOKEN_RBRACE,
    MINIC_TOKEN_SEMICOLON,
    MINIC_TOKEN_PLUS,
    MINIC_TOKEN_MINUS,
    MINIC_TOKEN_STAR,
    MINIC_TOKEN_SLASH,
    MINIC_TOKEN_PERCENT,
    MINIC_TOKEN_EQUAL,
    MINIC_TOKEN_KIND_COUNT
} MinicTokenKind;

typedef struct MinicToken {
    MinicTokenKind kind;
    MinicSourceSpan span;
} MinicToken;

const char *minic_token_kind_name(MinicTokenKind kind);

#endif
