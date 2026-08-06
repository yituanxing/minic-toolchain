#include "frontend/ast.h"
#include "frontend/ast_verifier.h"
#include "frontend/cast_normalization.h"

#include <stdbool.h>
#include <string.h>

static MinicSourceSpan test_span(size_t offset)
{
    MinicSourceSpan span;

    (void)memset(&span, 0, sizeof(span));
    span.begin.offset = offset;
    span.begin.line = 1U;
    span.begin.column = offset + 1U;
    span.end = span.begin;
    span.end.offset += 1U;
    span.end.column += 1U;
    return span;
}

static bool add_integer(
    MinicC0Program *program,
    int value,
    MinicExpressionId *expression_id)
{
    MinicExpression expression;

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_INTEGER;
    expression.span = test_span(program->expression_count);
    expression.type = minic_type_int();
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.integer_value = value;
    return minic_c0_program_add_expression(
        program,
        &expression,
        expression_id);
}

static bool add_cast(
    MinicC0Program *program,
    MinicExpressionId operand,
    MinicType target_type,
    MinicExpressionId *expression_id)
{
    MinicExpression expression;

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_CAST;
    expression.span = test_span(program->expression_count);
    expression.type = target_type;
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.unary.operand = operand;
    return minic_c0_program_add_expression(
        program,
        &expression,
        expression_id);
}

static bool add_expression_statement(
    MinicC0Program *program,
    MinicExpressionId expression_id,
    MinicStatementId *statement_id)
{
    MinicStatement statement;

    (void)memset(&statement, 0, sizeof(statement));
    statement.kind = MINIC_STATEMENT_EXPRESSION;
    statement.span = test_span(program->statement_count);
    statement.target_expression = MINIC_EXPRESSION_INVALID;
    statement.expression = expression_id;
    statement.then_block = MINIC_BLOCK_INVALID;
    statement.else_block = MINIC_BLOCK_INVALID;
    return minic_c0_program_add_statement(
        program,
        &statement,
        statement_id);
}

static int test_integer_cast_topology(void)
{
    MinicC0Program program;
    MinicExpressionId integer_id;
    MinicExpressionId cast_id;
    MinicStatementId statement_id;
    const MinicStatement *statement;
    const MinicExpression *normalized;

    minic_c0_program_initialize(&program);
    if (!add_integer(&program, 257, &integer_id) ||
        !add_cast(
            &program,
            integer_id,
            minic_type_unsigned_char(),
            &cast_id) ||
        !add_expression_statement(&program, cast_id, &statement_id) ||
        !minic_c0_program_verify(&program, MINIC_C0_AST_PARSED) ||
        minic_c0_program_verify(&program, MINIC_C0_AST_NORMALIZED) ||
        !minic_c0_program_normalize_casts(&program) ||
        !minic_c0_program_verify(&program, MINIC_C0_AST_NORMALIZED)) {
        minic_c0_program_destroy(&program);
        return 1;
    }

    statement = minic_c0_program_statement(&program, statement_id);
    normalized = statement == NULL
        ? NULL
        : minic_c0_program_expression(&program, statement->expression);
    if (program.expression_count != 3U || statement == NULL ||
        normalized == NULL ||
        normalized->kind != MINIC_EXPRESSION_BINARY ||
        normalized->value.binary.operator_kind != MINIC_BINARY_ADD ||
        normalized->value.binary.left >= statement->expression ||
        normalized->value.binary.right >= statement->expression ||
        !minic_type_equal(
            normalized->type,
            minic_type_unsigned_char())) {
        minic_c0_program_destroy(&program);
        return 2;
    }

    minic_c0_program_destroy(&program);
    return 0;
}

static int test_pointer_bitcast_identity(void)
{
    MinicC0Program program;
    MinicLocal local;
    MinicLocalId local_id;
    MinicExpression local_expression;
    MinicExpressionId local_expression_id;
    MinicExpressionId cast_id;
    MinicStatementId statement_id;
    MinicType int_pointer;
    MinicType void_pointer;
    const MinicStatement *statement;
    const MinicExpression *bitcast;
    const MinicExpression *operand;

    minic_c0_program_initialize(&program);
    if (!minic_type_pointer_to(minic_type_int(), &int_pointer) ||
        !minic_type_pointer_to(minic_type_void(), &void_pointer)) {
        return 3;
    }

    (void)memset(&local, 0, sizeof(local));
    local.name_span = test_span(0U);
    local.type = int_pointer;
    local.element_count = 1U;
    if (!minic_c0_program_add_local(&program, &local, &local_id)) {
        minic_c0_program_destroy(&program);
        return 4;
    }

    (void)memset(&local_expression, 0, sizeof(local_expression));
    local_expression.kind = MINIC_EXPRESSION_LOCAL;
    local_expression.span = test_span(0U);
    local_expression.type = int_pointer;
    local_expression.value_category = MINIC_VALUE_LVALUE;
    local_expression.value.local_id = local_id;
    if (!minic_c0_program_add_expression(
            &program,
            &local_expression,
            &local_expression_id) ||
        !add_cast(
            &program,
            local_expression_id,
            void_pointer,
            &cast_id) ||
        !add_expression_statement(&program, cast_id, &statement_id) ||
        !minic_c0_program_verify(&program, MINIC_C0_AST_PARSED) ||
        !minic_c0_program_normalize_casts(&program) ||
        minic_c0_program_verify(&program, MINIC_C0_AST_PARSED) ||
        !minic_c0_program_verify(&program, MINIC_C0_AST_NORMALIZED)) {
        minic_c0_program_destroy(&program);
        return 5;
    }

    statement = minic_c0_program_statement(&program, statement_id);
    bitcast = statement == NULL
        ? NULL
        : minic_c0_program_expression(&program, statement->expression);
    operand = bitcast == NULL
        ? NULL
        : minic_c0_program_expression(
              &program,
              bitcast->value.unary.operand);
    if (statement == NULL || bitcast == NULL || operand == NULL ||
        bitcast->kind != MINIC_EXPRESSION_BITCAST ||
        bitcast->value.unary.operand >= statement->expression ||
        !minic_type_equal(bitcast->type, void_pointer) ||
        operand->kind != MINIC_EXPRESSION_LOCAL ||
        !minic_type_equal(operand->type, int_pointer) ||
        operand->value.local_id != local_id) {
        minic_c0_program_destroy(&program);
        return 6;
    }

    minic_c0_program_destroy(&program);
    return 0;
}

static int test_malformed_contracts(void)
{
    MinicC0Program program;
    MinicExpression expression;
    MinicExpressionId integer_id;
    MinicExpressionId malformed_id;
    MinicStatementId statement_id;

    minic_c0_program_initialize(&program);
    if (!add_integer(&program, 1, &integer_id)) {
        return 7;
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_BINARY;
    expression.span = test_span(1U);
    expression.type = minic_type_int();
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.binary.operator_kind = MINIC_BINARY_ADD;
    expression.value.binary.left = integer_id;
    expression.value.binary.right = 1U;
    if (!minic_c0_program_add_expression(
            &program,
            &expression,
            &malformed_id) ||
        minic_c0_program_verify(&program, MINIC_C0_AST_PARSED)) {
        minic_c0_program_destroy(&program);
        return 8;
    }
    minic_c0_program_destroy(&program);

    minic_c0_program_initialize(&program);
    if (!add_integer(&program, 2, &integer_id)) {
        return 9;
    }
    program.expressions[integer_id].value_category = MINIC_VALUE_LVALUE;
    if (minic_c0_program_verify(&program, MINIC_C0_AST_PARSED)) {
        minic_c0_program_destroy(&program);
        return 10;
    }
    minic_c0_program_destroy(&program);

    minic_c0_program_initialize(&program);
    if (!add_integer(&program, 3, &integer_id) ||
        !add_expression_statement(
            &program,
            program.expression_count + 10U,
            &statement_id) ||
        minic_c0_program_verify(&program, MINIC_C0_AST_PARSED)) {
        minic_c0_program_destroy(&program);
        return 11;
    }
    minic_c0_program_destroy(&program);
    return 0;
}

static int test_normalization_transaction(void)
{
    MinicC0Program program;
    MinicExpressionId integer_id;
    MinicExpressionId cast_id;
    MinicStatementId statement_id;
    MinicExpression *original_expressions;
    size_t original_expression_count;
    MinicExpressionId original_statement_expression;

    minic_c0_program_initialize(&program);
    if (!add_integer(&program, 4, &integer_id) ||
        !add_cast(
            &program,
            integer_id,
            minic_type_unsigned_char(),
            &cast_id) ||
        !add_expression_statement(
            &program,
            program.expression_count + 20U,
            &statement_id)) {
        minic_c0_program_destroy(&program);
        return 12;
    }

    original_expressions = program.expressions;
    original_expression_count = program.expression_count;
    original_statement_expression =
        program.statements[statement_id].expression;
    if (minic_c0_program_normalize_casts(&program) ||
        program.expressions != original_expressions ||
        program.expression_count != original_expression_count ||
        program.statements[statement_id].expression !=
            original_statement_expression ||
        program.expressions[cast_id].kind != MINIC_EXPRESSION_CAST) {
        minic_c0_program_destroy(&program);
        return 13;
    }

    minic_c0_program_destroy(&program);
    return 0;
}

int main(void)
{
    int status;

    status = test_integer_cast_topology();
    if (status != 0) {
        return status;
    }
    status = test_pointer_bitcast_identity();
    if (status != 0) {
        return status;
    }
    status = test_malformed_contracts();
    if (status != 0) {
        return status;
    }
    return test_normalization_transaction();
}
