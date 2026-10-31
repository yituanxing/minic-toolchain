#ifndef MINIAS_EXPRESSION_GRAMMAR_H
#define MINIAS_EXPRESSION_GRAMMAR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


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

/* Syntax is parsed once, independently of absolute/relocatable arithmetic.
 * Symbol names are source spans until a consumer binds stable SymbolIDs;
 * the syntax tree itself is short lived and never stored in a Fragment. */
typedef enum MiniAsExprNodeKind {
    MINIAS_EXPR_LITERAL,
    MINIAS_EXPR_SYMBOL,
    MINIAS_EXPR_DOT,
    MINIAS_EXPR_UNARY,
    MINIAS_EXPR_BINARY
} MiniAsExprNodeKind;

typedef struct MiniAsExprNode {
    MiniAsExprNodeKind kind;
    MiniAsExprBinaryOp op;
    char unary;
    uint64_t literal;
    size_t left,right;
    size_t start,length;
} MiniAsExprNode;

/* Linux macro-expanded expressions can contain thousands of typed terms.
 * Keep a bounded graph rather than imposing the old 512-node stack limit. */
#define MINIAS_EXPR_MAX_NODES 8192U

typedef struct MiniAsExprSyntax {
    const char *text;
    MiniAsExprNode *nodes;
    size_t count,capacity,root;
    /* Most ISA immediates are one or two nodes; avoid heap allocation for
     * those hot paths while allowing larger GNU source expressions. */
    MiniAsExprNode inline_nodes[12];
} MiniAsExprSyntax;

bool minias_expr_parse_syntax(const char *text,MiniAsExprSyntax *out);
void minias_expr_syntax_destroy(MiniAsExprSyntax *syntax);
bool minias_expr_eval_integer(const MiniAsExprSyntax *syntax,
                              bool signed_shift,uint64_t *result);

/* Caller must pass an operator-start position; longest tokens win. */
bool minias_expr_scan_binary(const char *cursor, MiniAsExprBinary *out);

static inline void minias_expr_skip_space(const char **cursor) {
    while (**cursor == ' ' || **cursor == '\t') ++*cursor;
}

#endif
