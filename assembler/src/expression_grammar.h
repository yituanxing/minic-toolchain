#ifndef MINIAS_EXPRESSION_GRAMMAR_H
#define MINIAS_EXPRESSION_GRAMMAR_H

#include <stdbool.h>

/* Common GNU expression lexical grammar.  The same operator token and
 * binding power must be used for wrapping integer operands, checked
 * absolute/symbolic expressions, and retained deferred expression trees.
 * Each evaluator still owns its own arithmetic and relocation policy. */
typedef enum MiniAsExprBinaryOp {
    MINIAS_EXPR_OP_NONE = 0,
    MINIAS_EXPR_OP_LOR,
    MINIAS_EXPR_OP_LAND,
    MINIAS_EXPR_OP_OR,
    MINIAS_EXPR_OP_XOR,
    MINIAS_EXPR_OP_AND,
    MINIAS_EXPR_OP_EQ,
    MINIAS_EXPR_OP_NE,
    MINIAS_EXPR_OP_LT,
    MINIAS_EXPR_OP_LE,
    MINIAS_EXPR_OP_GT,
    MINIAS_EXPR_OP_GE,
    MINIAS_EXPR_OP_SHL,
    MINIAS_EXPR_OP_SHR,
    MINIAS_EXPR_OP_ADD,
    MINIAS_EXPR_OP_SUB,
    MINIAS_EXPR_OP_MUL,
    MINIAS_EXPR_OP_DIV,
    MINIAS_EXPR_OP_MOD
} MiniAsExprBinaryOp;

typedef struct MiniAsExprBinary {
    MiniAsExprBinaryOp op;
    unsigned precedence;
    unsigned width;
} MiniAsExprBinary;

/* Caller must pass an operator-start position; longest tokens win. */
bool minias_expr_scan_binary(const char *cursor, MiniAsExprBinary *out);

static inline void minias_expr_skip_space(const char **cursor) {
    while (**cursor == ' ' || **cursor == '\t') ++*cursor;
}

/* Consume only the requested precedence.  Lower-precedence operators remain
 * untouched so each parser's current grammar layer owns them. */
static inline bool minias_expr_take_binary(const char **cursor,
                                           unsigned precedence,
                                           MiniAsExprBinaryOp *op) {
    MiniAsExprBinary token;
    minias_expr_skip_space(cursor);
    if (!minias_expr_scan_binary(*cursor, &token) ||
        token.precedence != precedence) return false;
    *cursor += token.width;
    *op = token.op;
    return true;
}

#endif
