#include "frontend/ast.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool minic_grow_array(
    void **data,
    size_t *capacity,
    size_t count,
    size_t element_size)
{
    void *resized;
    size_t new_capacity;

    if (count < *capacity) {
        return true;
    }

    new_capacity = *capacity == 0U ? 16U : *capacity * 2U;
    if (new_capacity < *capacity ||
        new_capacity > SIZE_MAX / element_size) {
        return false;
    }

    resized = realloc(*data, new_capacity * element_size);
    if (resized == NULL) {
        return false;
    }
    *data = resized;
    *capacity = new_capacity;
    return true;
}

void minic_c0_program_initialize(MinicC0Program *program)
{
    (void)memset(program, 0, sizeof(*program));
    program->return_expression = MINIC_EXPRESSION_INVALID;
}

void minic_c0_program_destroy(MinicC0Program *program)
{
    free(program->expressions);
    free(program->locals);
    free(program->statements);
    minic_c0_program_initialize(program);
}

bool minic_c0_program_add_expression(
    MinicC0Program *program,
    const MinicExpression *expression,
    MinicExpressionId *expression_id)
{
    if (!minic_grow_array(
            (void **)&program->expressions,
            &program->expression_capacity,
            program->expression_count,
            sizeof(*program->expressions))) {
        return false;
    }

    *expression_id = program->expression_count;
    program->expressions[program->expression_count] = *expression;
    program->expression_count += 1U;
    return true;
}

bool minic_c0_program_add_local(
    MinicC0Program *program,
    const MinicLocal *local,
    MinicLocalId *local_id)
{
    if (!minic_grow_array(
            (void **)&program->locals,
            &program->local_capacity,
            program->local_count,
            sizeof(*program->locals))) {
        return false;
    }

    *local_id = program->local_count;
    program->locals[program->local_count] = *local;
    program->local_count += 1U;
    return true;
}

bool minic_c0_program_add_statement(
    MinicC0Program *program,
    const MinicStatement *statement,
    MinicStatementId *statement_id)
{
    if (!minic_grow_array(
            (void **)&program->statements,
            &program->statement_capacity,
            program->statement_count,
            sizeof(*program->statements))) {
        return false;
    }

    *statement_id = program->statement_count;
    program->statements[program->statement_count] = *statement;
    program->statement_count += 1U;
    return true;
}

const MinicExpression *minic_c0_program_expression(
    const MinicC0Program *program,
    MinicExpressionId expression_id)
{
    if (expression_id >= program->expression_count) {
        return NULL;
    }
    return &program->expressions[expression_id];
}

const MinicLocal *minic_c0_program_local(
    const MinicC0Program *program,
    MinicLocalId local_id)
{
    if (local_id >= program->local_count) {
        return NULL;
    }
    return &program->locals[local_id];
}

const MinicStatement *minic_c0_program_statement(
    const MinicC0Program *program,
    MinicStatementId statement_id)
{
    if (statement_id >= program->statement_count) {
        return NULL;
    }
    return &program->statements[statement_id];
}
