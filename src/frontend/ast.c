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

static char *minic_copy_name(const char *name, size_t name_length)
{
    char *copy;

    if (name == NULL || name_length == SIZE_MAX) {
        return NULL;
    }
    copy = (char *)malloc(name_length + 1U);
    if (copy == NULL) {
        return NULL;
    }
    (void)memcpy(copy, name, name_length);
    copy[name_length] = '\0';
    return copy;
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
    for (index = 0U; index < program->record_count; ++index) {
        MinicRecord *record;
        size_t field_index;

        record = &program->records[index];
        for (field_index = 0U;
             field_index < record->field_count;
             ++field_index) {
            free(record->fields[field_index].name);
        }
        free(record->fields);
        free(record->name);
    }
    free(program->expressions);
    free(program->locals);
    free(program->statements);
    free(program->blocks);
    free(program->functions);
    free(program->records);
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
        local_count > program->local_count - local_begin) {
        return false;
    }
    if (!minic_grow_array(
            (void **)&program->functions,
            &program->function_capacity,
            program->function_count,
            sizeof(*program->functions))) {
        return false;
    }

    function.name = minic_copy_name(name, name_length);
    if (function.name == NULL) {
        return false;
    }
    function.name_length = name_length;
    function.local_begin = local_begin;
    function.local_count = local_count;
    function.local_storage_size = 0U;
    function.parameter_count = 0U;
    function.body_block = body_block;
    function.is_defined = body_block != MINIC_BLOCK_INVALID;

    *function_id = program->function_count;
    program->functions[program->function_count] = function;
    program->function_count += 1U;
    return true;
}

bool minic_c0_program_set_function_parameter_count(
    MinicC0Program *program,
    MinicFunctionId function_id,
    size_t parameter_count)
{
    MinicFunction *function;

    if (function_id >= program->function_count || parameter_count > 8U) {
        return false;
    }
    function = &program->functions[function_id];
    if (function->is_defined &&
        (function->local_begin > program->local_count ||
         parameter_count > program->local_count - function->local_begin)) {
        return false;
    }
    function->parameter_count = parameter_count;
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
    function->local_storage_size = 0U;
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
    function->local_storage_size = 0U;
    return true;
}

bool minic_c0_program_add_record(
    MinicC0Program *program,
    const char *name,
    size_t name_length,
    MinicRecordId *record_id)
{
    MinicRecord record;
    size_t index;

    if (program == NULL || name == NULL || record_id == NULL) {
        return false;
    }
    for (index = 0U; index < program->record_count; ++index) {
        const MinicRecord *existing;

        existing = &program->records[index];
        if (existing->name_length == name_length &&
            memcmp(existing->name, name, name_length) == 0) {
            return false;
        }
    }
    if (!minic_grow_array(
            (void **)&program->records,
            &program->record_capacity,
            program->record_count,
            sizeof(*program->records))) {
        return false;
    }

    (void)memset(&record, 0, sizeof(record));
    record.name = minic_copy_name(name, name_length);
    if (record.name == NULL) {
        return false;
    }
    record.name_length = name_length;
    *record_id = program->record_count;
    program->records[program->record_count] = record;
    program->record_count += 1U;
    return true;
}

bool minic_c0_record_add_field(
    MinicC0Program *program,
    MinicRecordId record_id,
    const char *name,
    size_t name_length,
    MinicType type,
    size_t element_count)
{
    MinicRecord *record;
    MinicRecordField field;
    size_t index;

    if (program == NULL || record_id >= program->record_count ||
        name == NULL || element_count == 0U) {
        return false;
    }
    record = &program->records[record_id];
    if (record->is_complete) {
        return false;
    }
    for (index = 0U; index < record->field_count; ++index) {
        const MinicRecordField *existing;

        existing = &record->fields[index];
        if (existing->name_length == name_length &&
            memcmp(existing->name, name, name_length) == 0) {
            return false;
        }
    }
    if (!minic_grow_array(
            (void **)&record->fields,
            &record->field_capacity,
            record->field_count,
            sizeof(*record->fields))) {
        return false;
    }

    (void)memset(&field, 0, sizeof(field));
    field.name = minic_copy_name(name, name_length);
    if (field.name == NULL) {
        return false;
    }
    field.name_length = name_length;
    field.type = type;
    field.element_count = element_count;
    record->fields[record->field_count] = field;
    record->field_count += 1U;
    return true;
}

bool minic_c0_program_finish_record(
    MinicC0Program *program,
    MinicRecordId record_id)
{
    MinicRecord *record;

    if (program == NULL || record_id >= program->record_count) {
        return false;
    }
    record = &program->records[record_id];
    if (record->is_complete || record->field_count == 0U) {
        return false;
    }
    record->is_complete = true;
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

const MinicRecord *minic_c0_program_record(
    const MinicC0Program *program,
    MinicRecordId record_id)
{
    if (program == NULL || record_id >= program->record_count) {
        return NULL;
    }
    return &program->records[record_id];
}

const MinicRecordField *minic_c0_record_field(
    const MinicRecord *record,
    size_t field_index)
{
    if (record == NULL || field_index >= record->field_count) {
        return NULL;
    }
    return &record->fields[field_index];
}
