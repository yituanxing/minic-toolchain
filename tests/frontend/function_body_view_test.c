#include "frontend/ast.h"
#include "frontend/ast_verifier.h"
#include "frontend/function_body.h"

#include <string.h>

static MinicSourceSpan test_span(size_t offset) {
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

static void initialize_statement(MinicStatement *statement, MinicStatementKind kind) {
    (void)memset(statement, 0, sizeof(*statement));
    statement->kind = kind;
    statement->target_expression = MINIC_EXPRESSION_INVALID;
    statement->expression = MINIC_EXPRESSION_INVALID;
    statement->target_statement = MINIC_STATEMENT_INVALID;
    statement->inline_asm_id = MINIC_INLINE_ASM_INVALID;
    statement->cleanup_context = MINIC_CLEANUP_CONTEXT_ROOT;
    statement->cleanup_stop_context = MINIC_CLEANUP_CONTEXT_ROOT;
    statement->then_block = MINIC_BLOCK_INVALID;
    statement->else_block = MINIC_BLOCK_INVALID;
}

static bool add_local(MinicC0Program *program, MinicLocalId *local_id) {
    MinicLocal local;

    (void)memset(&local, 0, sizeof(local));
    local.name_span = test_span(program->local_count);
    local.type = minic_type_int();
    local.element_count = 1U;
    return minic_c0_program_add_local(program, &local, local_id);
}

static bool add_local_expression(MinicC0Program *program,
                                 MinicLocalId local_id,
                                 MinicExpressionId *expression_id) {
    MinicExpression expression;

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_LOCAL;
    expression.span = test_span(program->expression_count);
    expression.type = minic_type_int();
    expression.value_category = MINIC_VALUE_LVALUE;
    expression.value.local_id = local_id;
    return minic_c0_program_add_expression(program, &expression, expression_id);
}

static bool add_integer_expression(MinicC0Program *program,
                                   int value,
                                   MinicExpressionId *expression_id) {
    MinicExpression expression;

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_INTEGER;
    expression.span = test_span(program->expression_count);
    expression.type = minic_type_int();
    expression.value_category = MINIC_VALUE_RVALUE;
    expression.value.integer_value = value;
    return minic_c0_program_add_expression(program, &expression, expression_id);
}

static bool add_expression_statement(MinicC0Program *program,
                                     MinicBlockId block_id,
                                     MinicExpressionId expression_id,
                                     MinicStatementId *statement_id) {
    MinicStatement statement;

    initialize_statement(&statement, MINIC_STATEMENT_EXPRESSION);
    statement.span = test_span(program->statement_count);
    statement.expression = expression_id;
    return minic_c0_program_add_statement(program, &statement, statement_id) &&
           minic_c0_block_add_statement(program, block_id, *statement_id);
}

static bool add_void_function(MinicC0Program *program,
                              const char *name,
                              size_t name_length,
                              MinicLocalId local_id,
                              MinicBlockId body_block,
                              MinicFunctionId *function_id) {
    MinicType return_type;

    return_type = minic_type_void();
    return minic_c0_program_add_function(
               program, name, name_length, local_id, 1U, body_block, function_id) &&
           minic_c0_program_set_function_signature(program, *function_id, return_type, NULL, 0U);
}

static int test_cross_function_local_rejected(void) {
    MinicC0Program program;
    MinicBlockId block_a;
    MinicBlockId block_b;
    MinicLocalId local_a;
    MinicLocalId local_b;
    MinicExpressionId expression_a;
    MinicExpressionId expression_b;
    MinicStatementId statement_a;
    MinicStatementId statement_b;
    MinicFunctionId function_a;
    MinicFunctionId function_b;

    minic_c0_program_initialize(&program);
    if (!minic_c0_program_add_block(&program, &block_a) || !add_local(&program, &local_a) ||
        !add_local_expression(&program, local_a, &expression_a) ||
        !add_expression_statement(&program, block_a, expression_a, &statement_a) ||
        !add_void_function(&program, "a", 1U, local_a, block_a, &function_a) ||
        !minic_c0_program_add_block(&program, &block_b) || !add_local(&program, &local_b) ||
        !add_local_expression(&program, local_b, &expression_b) ||
        !add_expression_statement(&program, block_b, expression_b, &statement_b) ||
        !add_void_function(&program, "b", 1U, local_b, block_b, &function_b) ||
        !minic_c0_program_verify(&program, MINIC_C0_AST_PARSED) ||
        !minic_c0_program_validate_function_body_ownership(&program)) {
        minic_c0_program_destroy(&program);
        return 1;
    }

    program.expressions[expression_a].value.local_id = local_b;
    if (!minic_c0_program_verify(&program, MINIC_C0_AST_PARSED) ||
        minic_c0_program_validate_function_body_ownership(&program)) {
        minic_c0_program_destroy(&program);
        return 2;
    }

    (void)statement_a;
    (void)statement_b;
    (void)function_a;
    (void)function_b;
    minic_c0_program_destroy(&program);
    return 0;
}

static int test_cross_function_goto_rejected(void) {
    MinicC0Program program;
    MinicBlockId block_a;
    MinicBlockId block_b;
    MinicLocalId local_a;
    MinicLocalId local_b;
    MinicStatement label_a;
    MinicStatement label_b;
    MinicStatement go_to;
    MinicStatementId label_a_id;
    MinicStatementId label_b_id;
    MinicStatementId goto_id;
    MinicFunctionId function_a;
    MinicFunctionId function_b;

    minic_c0_program_initialize(&program);
    if (!minic_c0_program_add_block(&program, &block_a) || !add_local(&program, &local_a)) {
        return 3;
    }
    initialize_statement(&label_a, MINIC_STATEMENT_LABEL);
    label_a.span = test_span(0U);
    if (!minic_c0_program_add_statement(&program, &label_a, &label_a_id) ||
        !minic_c0_block_add_statement(&program, block_a, label_a_id) ||
        !add_void_function(&program, "a", 1U, local_a, block_a, &function_a) ||
        !minic_c0_program_add_block(&program, &block_b) || !add_local(&program, &local_b)) {
        minic_c0_program_destroy(&program);
        return 4;
    }
    initialize_statement(&label_b, MINIC_STATEMENT_LABEL);
    label_b.span = test_span(1U);
    if (!minic_c0_program_add_statement(&program, &label_b, &label_b_id) ||
        !minic_c0_block_add_statement(&program, block_b, label_b_id)) {
        minic_c0_program_destroy(&program);
        return 5;
    }
    initialize_statement(&go_to, MINIC_STATEMENT_GOTO);
    go_to.span = test_span(2U);
    go_to.target_statement = label_b_id;
    if (!minic_c0_program_add_statement(&program, &go_to, &goto_id) ||
        !minic_c0_block_add_statement(&program, block_a, goto_id) ||
        !add_void_function(&program, "b", 1U, local_b, block_b, &function_b) ||
        !minic_c0_program_verify(&program, MINIC_C0_AST_PARSED) ||
        minic_c0_program_validate_function_body_ownership(&program)) {
        minic_c0_program_destroy(&program);
        return 6;
    }

    program.statements[goto_id].target_statement = label_a_id;
    if (!minic_c0_program_verify(&program, MINIC_C0_AST_PARSED) ||
        !minic_c0_program_validate_function_body_ownership(&program)) {
        minic_c0_program_destroy(&program);
        return 7;
    }

    (void)function_a;
    (void)function_b;
    minic_c0_program_destroy(&program);
    return 0;
}

static int test_orphan_constant_expression_allowed(void) {
    MinicC0Program program;
    MinicBlockId block;
    MinicLocalId local_id;
    MinicExpressionId orphan_id;
    MinicFunctionId function_id;

    minic_c0_program_initialize(&program);
    if (!add_integer_expression(&program, 7, &orphan_id) ||
        !minic_c0_program_add_block(&program, &block) || !add_local(&program, &local_id) ||
        !add_void_function(&program, "f", 1U, local_id, block, &function_id) ||
        !minic_c0_program_verify(&program, MINIC_C0_AST_PARSED) ||
        !minic_c0_program_validate_function_body_ownership(&program)) {
        minic_c0_program_destroy(&program);
        return 8;
    }

    (void)orphan_id;
    (void)function_id;
    minic_c0_program_destroy(&program);
    return 0;
}

int main(void) {
    int status;

    status = test_cross_function_local_rejected();
    if (status != 0) {
        return status;
    }
    status = test_cross_function_goto_rejected();
    if (status != 0) {
        return status;
    }
    return test_orphan_constant_expression_allowed();
}
