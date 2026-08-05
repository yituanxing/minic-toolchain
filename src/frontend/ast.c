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
    program->body_block = MINIC_BLOCK_INVALID;
    program->entry_function = MINIC_FUNCTION_INVALID;
    program->return_expression = MINIC_EXPRESSION_INVALID;
}

void minic_c0_program_destroy(MinicC0Program *program)
{
    size_t index;

    for (index = 0U; index < program->block_count; ++index) {
        free(program->blocks[index].statements);
    }
    for (index = 0U; index < program->function_count; ++index) {
        free(program->functions[index].name);
    }
    free(program->expressions);
    free(program->locals);
    free(program->statements);
    free(program->blocks);
    free(program->functions);
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

bool minic_c0_program_add_block(
    MinicC0Program *program,
    MinicBlockId *block_id)
{
    MinicBlock block;

    if (!minic_grow_array(
            (void **)&program->blocks,
            &program->block_capacity,
            program->block_count,
            sizeof(*program->blocks))) {
        return false;
    }

    (void)memset(&block, 0, sizeof(block));
    *block_id = program->block_count;
    program->blocks[program->block_count] = block;
    program->block_count += 1U;
    return true;
}

bool minic_c0_block_add_statement(
    MinicC0Program *program,
    MinicBlockId block_id,
    MinicStatementId statement_id)
{
    MinicBlock *block;

    if (block_id >= program->block_count ||
        statement_id >= program->statement_count) {
        return false;
    }

    block = &program->blocks[block_id];
    if (!minic_grow_array(
            (void **)&block->statements,
            &block->statement_capacity,
            block->statement_count,
            sizeof(*block->statements))) {
        return false;
    }

    block->statements[block->statement_count] = statement_id;
    block->statement_count += 1U;
    return true;
}

bool minic_c0_program_add_function(
    MinicC0Program *program,
    const char *name,
    size_t name_length,
    size_t local_begin,
    size_t local_count,
    MinicBlockId body_block,
    MinicFunctionId *function_id)
{
    MinicFunction function;

    if (name == NULL || function_id == NULL ||
        (body_block != MINIC_BLOCK_INVALID &&
         body_block >= program->block_count) ||
        local_begin > program->local_count ||
        local_count > program->local_count - local_begin ||
        name_length == SIZE_MAX) {
        return false;
    }
    if (!minic_grow_array(
            (void **)&program->functions,
            &program->function_capacity,
            program->function_count,
            sizeof(*program->functions))) {
        return false;
    }

    function.name = (char *)malloc(name_length + 1U);
    if (function.name == NULL) {
        return false;
    }
    (void)memcpy(function.name, name, name_length);
    function.name[name_length] = '\0';
    function.name_length = name_length;
    function.local_begin = local_begin;
    function.local_count = local_count;
    function.body_block = body_block;
    function.is_defined = body_block != MINIC_BLOCK_INVALID;

    *function_id = program->function_count;
    program->functions[program->function_count] = function;
    program->function_count += 1U;
    return true;
}

bool minic_c0_program_define_function(
    MinicC0Program *program,
    MinicFunctionId function_id,
    size_t local_begin,
    MinicBlockId body_block)
{
    MinicFunction *function;

    if (function_id >= program->function_count ||
        body_block >= program->block_count ||
        local_begin > program->local_count) {
        return false;
    }
    function = &program->functions[function_id];
    if (function->is_defined) {
        return false;
    }
    function->local_begin = local_begin;
    function->local_count = 0U;
    function->body_block = body_block;
    function->is_defined = true;
    return true;
}

bool minic_c0_program_finish_function(
    MinicC0Program *program,
    MinicFunctionId function_id,
    size_t local_count)
{
    MinicFunction *function;

    if (function_id >= program->function_count) {
        return false;
    }
    function = &program->functions[function_id];
    if (!function->is_defined ||
        function->body_block >= program->block_count ||
        function->local_begin > program->local_count ||
        local_count > program->local_count - function->local_begin) {
        return false;
    }
    function->local_count = local_count;
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

const MinicBlock *minic_c0_program_block(
    const MinicC0Program *program,
    MinicBlockId block_id)
{
    if (block_id >= program->block_count) {
        return NULL;
    }
    return &program->blocks[block_id];
}

const MinicFunction *minic_c0_program_function(
    const MinicC0Program *program,
    MinicFunctionId function_id)
{
    if (function_id >= program->function_count) {
        return NULL;
    }
    return &program->functions[function_id];
}
