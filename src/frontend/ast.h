#ifndef MINIC_FRONTEND_AST_H
#define MINIC_FRONTEND_AST_H

#include "frontend/token.h"
#include "frontend/type.h"

#include <stdbool.h>
#include <stddef.h>

typedef size_t MinicExpressionId;
typedef size_t MinicLocalId;
typedef size_t MinicStatementId;
typedef size_t MinicBlockId;
typedef size_t MinicFunctionId;

#define MINIC_EXPRESSION_INVALID ((MinicExpressionId)-1)
#define MINIC_LOCAL_INVALID ((MinicLocalId)-1)
#define MINIC_STATEMENT_INVALID ((MinicStatementId)-1)
#define MINIC_BLOCK_INVALID ((MinicBlockId)-1)
#define MINIC_FUNCTION_INVALID ((MinicFunctionId)-1)

typedef enum MinicValueCategory {
    MINIC_VALUE_RVALUE = 0,
    MINIC_VALUE_LVALUE
} MinicValueCategory;

typedef enum MinicExpressionKind {
    MINIC_EXPRESSION_INTEGER = 0,
    MINIC_EXPRESSION_LOCAL,
    MINIC_EXPRESSION_ADDRESS_OF,
    MINIC_EXPRESSION_DEREFERENCE,
    MINIC_EXPRESSION_UNARY,
    MINIC_EXPRESSION_BINARY,
    MINIC_EXPRESSION_CALL
} MinicExpressionKind;

typedef enum MinicUnaryOperator {
    MINIC_UNARY_PLUS = 0,
    MINIC_UNARY_NEGATE,
    MINIC_UNARY_LOGICAL_NOT
} MinicUnaryOperator;

typedef enum MinicBinaryOperator {
    MINIC_BINARY_ADD = 0,
    MINIC_BINARY_SUBTRACT,
    MINIC_BINARY_MULTIPLY,
    MINIC_BINARY_DIVIDE,
    MINIC_BINARY_REMAINDER,
    MINIC_BINARY_EQUAL,
    MINIC_BINARY_NOT_EQUAL,
    MINIC_BINARY_LESS,
    MINIC_BINARY_LESS_EQUAL,
    MINIC_BINARY_GREATER,
    MINIC_BINARY_GREATER_EQUAL
} MinicBinaryOperator;

typedef struct MinicExpression {
    MinicExpressionKind kind;
    MinicSourceSpan span;
    MinicType type;
    MinicValueCategory value_category;
    union {
        int integer_value;
        MinicLocalId local_id;
        struct {
            MinicFunctionId function_id;
            size_t argument_count;
            MinicExpressionId arguments[8];
        } call;
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

typedef struct MinicLocal {
    MinicSourceSpan name_span;
    MinicType type;
    size_t storage_offset;
} MinicLocal;

typedef enum MinicStatementKind {
    MINIC_STATEMENT_ASSIGN = 0,
    MINIC_STATEMENT_RETURN,
    MINIC_STATEMENT_IF,
    MINIC_STATEMENT_WHILE
} MinicStatementKind;

typedef struct MinicStatement {
    MinicStatementKind kind;
    MinicSourceSpan span;
    MinicExpressionId target_expression;
    MinicExpressionId expression;
    MinicBlockId then_block;
    MinicBlockId else_block;
} MinicStatement;

typedef struct MinicBlock {
    MinicStatementId *statements;
    size_t statement_count;
    size_t statement_capacity;
} MinicBlock;

typedef struct MinicFunction {
    char *name;
    size_t name_length;
    size_t local_begin;
    size_t local_count;
    size_t local_storage_size;
    size_t parameter_count;
    MinicBlockId body_block;
    bool is_defined;
} MinicFunction;

typedef struct MinicC0Program {
    MinicExpression *expressions;
    size_t expression_count;
    size_t expression_capacity;

    MinicLocal *locals;
    size_t local_count;
    size_t local_capacity;

    MinicStatement *statements;
    size_t statement_count;
    size_t statement_capacity;

    MinicBlock *blocks;
    size_t block_count;
    size_t block_capacity;
    MinicBlockId body_block;

    MinicFunction *functions;
    size_t function_count;
    size_t function_capacity;
    MinicFunctionId entry_function;

    MinicExpressionId return_expression;
} MinicC0Program;

void minic_c0_program_initialize(MinicC0Program *program);
void minic_c0_program_destroy(MinicC0Program *program);

bool minic_c0_program_add_expression(
    MinicC0Program *program,
    const MinicExpression *expression,
    MinicExpressionId *expression_id);
bool minic_c0_program_add_local(
    MinicC0Program *program,
    const MinicLocal *local,
    MinicLocalId *local_id);
bool minic_c0_program_add_statement(
    MinicC0Program *program,
    const MinicStatement *statement,
    MinicStatementId *statement_id);
bool minic_c0_program_add_block(
    MinicC0Program *program,
    MinicBlockId *block_id);
bool minic_c0_block_add_statement(
    MinicC0Program *program,
    MinicBlockId block_id,
    MinicStatementId statement_id);
bool minic_c0_program_add_function(
    MinicC0Program *program,
    const char *name,
    size_t name_length,
    size_t local_begin,
    size_t local_count,
    MinicBlockId body_block,
    MinicFunctionId *function_id);
bool minic_c0_program_set_function_parameter_count(
    MinicC0Program *program,
    MinicFunctionId function_id,
    size_t parameter_count);
bool minic_c0_program_define_function(
    MinicC0Program *program,
    MinicFunctionId function_id,
    size_t local_begin,
    MinicBlockId body_block);
bool minic_c0_program_finish_function(
    MinicC0Program *program,
    MinicFunctionId function_id,
    size_t local_count);

const MinicExpression *minic_c0_program_expression(
    const MinicC0Program *program,
    MinicExpressionId expression_id);
const MinicLocal *minic_c0_program_local(
    const MinicC0Program *program,
    MinicLocalId local_id);
const MinicStatement *minic_c0_program_statement(
    const MinicC0Program *program,
    MinicStatementId statement_id);
const MinicBlock *minic_c0_program_block(
    const MinicC0Program *program,
    MinicBlockId block_id);
const MinicFunction *minic_c0_program_function(
    const MinicC0Program *program,
    MinicFunctionId function_id);

#endif
