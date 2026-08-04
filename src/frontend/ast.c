#include "frontend/ast.h"

#include <stdint.h>
#include <stdlib.h>

void minic_c0_program_initialize(MinicC0Program *program)
{
    program->expressions = NULL;
    program->expression_count = 0U;
    program->expression_capacity = 0U;
    program->return_expression = MINIC_EXPRESSION_INVALID;
}

void minic_c0_program_destroy(MinicC0Program *program)
{
    free(program->expressions);
    minic_c0_program_initialize(program);
}

bool minic_c0_program_add_expression(
    MinicC0Program *program,
    const MinicExpression *expression,
    MinicExpressionId *expression_id)
{
    MinicExpression *resized;
    size_t new_capacity;

    if (program->expression_count == program->expression_capacity) {
        new_capacity = program->expression_capacity == 0U
            ? 16U
            : program->expression_capacity * 2U;
        if (new_capacity < program->expression_capacity ||
            new_capacity > SIZE_MAX / sizeof(*program->expressions)) {
            return false;
        }
        resized = (MinicExpression *)realloc(
            program->expressions,
            new_capacity * sizeof(*program->expressions));
        if (resized == NULL) {
            return false;
        }
        program->expressions = resized;
        program->expression_capacity = new_capacity;
    }

    *expression_id = program->expression_count;
    program->expressions[program->expression_count] = *expression;
    program->expression_count += 1U;
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
