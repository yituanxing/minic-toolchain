#ifndef MINIC_FRONTEND_AST_H
#define MINIC_FRONTEND_AST_H

#include "frontend/token.h"

#include <stdbool.h>
#include <stddef.h>

typedef size_t MinicExpressionId;

#define MINIC_EXPRESSION_INVALID ((MinicExpressionId)-1)

typedef enum MinicExpressionKind {
    MINIC_EXPRESSION_INTEGER = 0,
    MINIC_EXPRESSION_UNARY,
    MINIC_EXPRESSION_BINARY
} MinicExpressionKind;

typedef enum MinicUnaryOperator {
    MINIC_UNARY_PLUS = 0,
    MINIC_UNARY_NEGATE
} MinicUnaryOperator;

typedef enum MinicBinaryOperator {
    MINIC_BINARY_ADD = 0,
    MINIC_BINARY_SUBTRACT,
    MINIC_BINARY_MULTIPLY,
    MINIC_BINARY_DIVIDE,
    MINIC_BINARY_REMAINDER
} MinicBinaryOperator;

typedef struct MinicExpression {
    MinicExpressionKind kind;
    MinicSourceSpan span;
    union {
        int integer_value;
        struct {
            MinicUnaryOperator operator_kind;
            MinicExpressionId operand;
        } unary;
        struct {
            MinicBinaryOperator operator_kind;
            MinicExpressionId left;
            MinicExpressionId right;
        } binary;
    } value;
} MinicExpression;

typedef struct MinicC0Program {
    MinicExpression *expressions;
    size_t expression_count;
    size_t expression_capacity;
    MinicExpressionId return_expression;
} MinicC0Program;

void minic_c0_program_initialize(MinicC0Program *program);
void minic_c0_program_destroy(MinicC0Program *program);
bool minic_c0_program_add_expression(
    MinicC0Program *program,
    const MinicExpression *expression,
    MinicExpressionId *expression_id);
const MinicExpression *minic_c0_program_expression(
    const MinicC0Program *program,
    MinicExpressionId expression_id);

#endif
