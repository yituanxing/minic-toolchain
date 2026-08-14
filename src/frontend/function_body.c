#include "frontend/function_body.h"

#include "frontend/ast_traversal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef bool (*MinicExpressionIdReadVisitor)(MinicExpressionId expression_id, void *context);

typedef struct MinicExpressionIdReadContext {
    MinicExpressionIdReadVisitor visitor;
    void *context;
} MinicExpressionIdReadContext;

typedef struct MinicFunctionBodyValidation {
    const MinicC0Program *program;
    MinicFunctionId *block_owners;
    MinicFunctionId *statement_owners;
    MinicFunctionId *local_owners;
    MinicFunctionId *cleanup_context_owners;
    MinicFunctionId *inline_asm_owners;
    size_t *expression_generations;
    MinicBlockId *block_work;
    MinicStatementId *statement_work;
    MinicExpressionId *expression_work;
    size_t block_work_count;
    size_t block_work_cursor;
    size_t statement_work_count;
    size_t expression_work_count;
    size_t expression_work_cursor;
    size_t expression_generation;
    MinicFunctionId function_id;
} MinicFunctionBodyValidation;

static bool storage_shape_is_valid(const void *data, size_t count, size_t capacity) {
    return count <= capacity && (count == 0U || data != NULL);
}

bool minic_c0_function_body_view(const MinicC0Program *program,
                                 MinicFunctionId function_id,
                                 MinicFunctionBodyView *view) {
    const MinicFunction *function;

    if (program == NULL || view == NULL) {
        return false;
    }
    function = minic_c0_program_function(program, function_id);
    if (function == NULL || !function->is_defined || function->body_block >= program->block_count ||
        function->local_begin > program->local_count ||
        function->local_count > program->local_count - function->local_begin) {
        return false;
    }
    view->program = program;
    view->function_id = function_id;
    return true;
}

const MinicFunction *minic_c0_function_body_function(const MinicFunctionBodyView *view) {
    if (view == NULL || view->program == NULL) {
        return NULL;
    }
    return minic_c0_program_function(view->program, view->function_id);
}

MinicBlockId minic_c0_function_body_root_block(const MinicFunctionBodyView *view) {
    const MinicFunction *function;

    function = minic_c0_function_body_function(view);
    return function != NULL && function->is_defined ? function->body_block : MINIC_BLOCK_INVALID;
}

bool minic_c0_function_body_owns_local(const MinicFunctionBodyView *view, MinicLocalId local_id) {
    const MinicFunction *function;

    function = minic_c0_function_body_function(view);
    return function != NULL && function->is_defined && local_id >= function->local_begin &&
           local_id - function->local_begin < function->local_count;
}

static bool visit_read_expression_id(MinicExpressionId *expression_id, void *opaque_context) {
    MinicExpressionIdReadContext *context;

    if (expression_id == NULL || opaque_context == NULL) {
        return false;
    }
    context = (MinicExpressionIdReadContext *)opaque_context;
    return context->visitor(*expression_id, context->context);
}

static bool visit_expression_child_ids(const MinicExpression *expression,
                                       MinicExpressionIdReadVisitor visitor,
                                       void *context) {
    MinicExpression copy;
    MinicExpressionIdReadContext read_context;

    if (expression == NULL || visitor == NULL) {
        return false;
    }
    copy = *expression;
    read_context.visitor = visitor;
    read_context.context = context;
    return minic_c0_expression_visit_child_id_refs(&copy, visit_read_expression_id, &read_context);
}

static bool validation_storage_is_valid(const MinicC0Program *program) {
    return program != NULL &&
           storage_shape_is_valid(
               program->expressions, program->expression_count, program->expression_capacity) &&
           storage_shape_is_valid(program->locals, program->local_count, program->local_capacity) &&
           storage_shape_is_valid(program->cleanup_contexts,
                                  program->cleanup_context_count,
                                  program->cleanup_context_capacity) &&
           storage_shape_is_valid(
               program->statements, program->statement_count, program->statement_capacity) &&
           storage_shape_is_valid(
               program->inline_asms, program->inline_asm_count, program->inline_asm_capacity) &&
           storage_shape_is_valid(program->blocks, program->block_count, program->block_capacity) &&
           storage_shape_is_valid(
               program->functions, program->function_count, program->function_capacity);
}

static bool allocate_array(void **storage, size_t count, size_t element_size) {
    if (storage == NULL || element_size == 0U || count > SIZE_MAX / element_size) {
        return false;
    }
    *storage = count == 0U ? NULL : malloc(count * element_size);
    return count == 0U || *storage != NULL;
}

static void destroy_validation(MinicFunctionBodyValidation *validation) {
    if (validation == NULL) {
        return;
    }
    free(validation->block_owners);
    free(validation->statement_owners);
    free(validation->local_owners);
    free(validation->cleanup_context_owners);
    free(validation->inline_asm_owners);
    free(validation->expression_generations);
    free(validation->block_work);
    free(validation->statement_work);
    free(validation->expression_work);
    (void)memset(validation, 0, sizeof(*validation));
}

static bool initialize_validation(const MinicC0Program *program,
                                  MinicFunctionBodyValidation *validation) {
    size_t index;

    if (validation == NULL || !validation_storage_is_valid(program)) {
        return false;
    }
    (void)memset(validation, 0, sizeof(*validation));
    validation->program = program;
    if (!allocate_array((void **)&validation->block_owners,
                        program->block_count,
                        sizeof(*validation->block_owners)) ||
        !allocate_array((void **)&validation->statement_owners,
                        program->statement_count,
                        sizeof(*validation->statement_owners)) ||
        !allocate_array((void **)&validation->local_owners,
                        program->local_count,
                        sizeof(*validation->local_owners)) ||
        !allocate_array((void **)&validation->cleanup_context_owners,
                        program->cleanup_context_count,
                        sizeof(*validation->cleanup_context_owners)) ||
        !allocate_array((void **)&validation->inline_asm_owners,
                        program->inline_asm_count,
                        sizeof(*validation->inline_asm_owners)) ||
        !allocate_array((void **)&validation->expression_generations,
                        program->expression_count,
                        sizeof(*validation->expression_generations)) ||
        !allocate_array((void **)&validation->block_work,
                        program->block_count,
                        sizeof(*validation->block_work)) ||
        !allocate_array((void **)&validation->statement_work,
                        program->statement_count,
                        sizeof(*validation->statement_work)) ||
        !allocate_array((void **)&validation->expression_work,
                        program->expression_count,
                        sizeof(*validation->expression_work))) {
        destroy_validation(validation);
        return false;
    }
    for (index = 0U; index < program->block_count; ++index) {
        validation->block_owners[index] = MINIC_FUNCTION_INVALID;
    }
    for (index = 0U; index < program->statement_count; ++index) {
        validation->statement_owners[index] = MINIC_FUNCTION_INVALID;
    }
    for (index = 0U; index < program->local_count; ++index) {
        validation->local_owners[index] = MINIC_FUNCTION_INVALID;
    }
    for (index = 0U; index < program->cleanup_context_count; ++index) {
        validation->cleanup_context_owners[index] = MINIC_FUNCTION_INVALID;
    }
    for (index = 0U; index < program->inline_asm_count; ++index) {
        validation->inline_asm_owners[index] = MINIC_FUNCTION_INVALID;
    }
    if (program->expression_count != 0U) {
        (void)memset(validation->expression_generations,
                     0,
                     program->expression_count * sizeof(*validation->expression_generations));
    }
    return true;
}

static bool assign_local_owners(MinicFunctionBodyValidation *validation) {
    const MinicC0Program *program;
    size_t function_index;

    if (validation == NULL || validation->program == NULL) {
        return false;
    }
    program = validation->program;
    for (function_index = 0U; function_index < program->function_count; ++function_index) {
        const MinicFunction *function;
        size_t local_index;

        function = &program->functions[function_index];
        if (!function->is_defined) {
            continue;
        }
        if (function->body_block >= program->block_count ||
            function->local_begin > program->local_count ||
            function->local_count > program->local_count - function->local_begin ||
            function->parameter_count > function->local_count) {
            return false;
        }
        for (local_index = 0U; local_index < function->local_count; ++local_index) {
            MinicLocalId local_id;

            local_id = function->local_begin + local_index;
            if (validation->local_owners[local_id] != MINIC_FUNCTION_INVALID) {
                return false;
            }
            validation->local_owners[local_id] = function_index;
        }
    }
    return true;
}

static bool claim_block(MinicFunctionBodyValidation *validation, MinicBlockId block_id) {
    const MinicC0Program *program;

    if (validation == NULL || validation->program == NULL) {
        return false;
    }
    program = validation->program;
    if (block_id >= program->block_count ||
        validation->block_owners[block_id] != MINIC_FUNCTION_INVALID ||
        validation->block_work_count >= program->block_count) {
        return false;
    }
    validation->block_owners[block_id] = validation->function_id;
    validation->block_work[validation->block_work_count++] = block_id;
    return true;
}

static bool claim_statement(MinicFunctionBodyValidation *validation,
                            MinicStatementId statement_id) {
    const MinicC0Program *program;

    if (validation == NULL || validation->program == NULL) {
        return false;
    }
    program = validation->program;
    if (statement_id >= program->statement_count ||
        validation->statement_owners[statement_id] != MINIC_FUNCTION_INVALID ||
        validation->statement_work_count >= program->statement_count) {
        return false;
    }
    validation->statement_owners[statement_id] = validation->function_id;
    validation->statement_work[validation->statement_work_count++] = statement_id;
    return true;
}

static bool claim_cleanup_context(MinicFunctionBodyValidation *validation,
                                  MinicCleanupContextId cleanup_context_id) {
    MinicFunctionId *owner;

    if (validation == NULL || validation->program == NULL ||
        cleanup_context_id > validation->program->cleanup_context_count) {
        return false;
    }
    if (cleanup_context_id == MINIC_CLEANUP_CONTEXT_ROOT) {
        return true;
    }
    owner = &validation->cleanup_context_owners[cleanup_context_id - 1U];
    if (*owner == MINIC_FUNCTION_INVALID) {
        *owner = validation->function_id;
        return true;
    }
    return *owner == validation->function_id;
}

static bool claim_inline_asm(MinicFunctionBodyValidation *validation,
                             MinicInlineAsmId inline_asm_id) {
    MinicFunctionId *owner;

    if (validation == NULL || validation->program == NULL ||
        inline_asm_id >= validation->program->inline_asm_count) {
        return false;
    }
    owner = &validation->inline_asm_owners[inline_asm_id];
    if (*owner == MINIC_FUNCTION_INVALID) {
        *owner = validation->function_id;
        return true;
    }
    return *owner == validation->function_id;
}

static bool enqueue_expression(MinicFunctionBodyValidation *validation,
                               MinicExpressionId expression_id) {
    const MinicC0Program *program;

    if (validation == NULL || validation->program == NULL) {
        return false;
    }
    program = validation->program;
    if (expression_id >= program->expression_count) {
        return false;
    }
    if (validation->expression_generations[expression_id] == validation->expression_generation) {
        return true;
    }
    if (validation->expression_work_count >= program->expression_count) {
        return false;
    }
    validation->expression_generations[expression_id] = validation->expression_generation;
    validation->expression_work[validation->expression_work_count++] = expression_id;
    return true;
}

static bool enqueue_expression_visitor(MinicExpressionId expression_id, void *opaque_context) {
    return enqueue_expression((MinicFunctionBodyValidation *)opaque_context, expression_id);
}

static bool enqueue_optional_expression(MinicFunctionBodyValidation *validation,
                                        MinicExpressionId expression_id) {
    return expression_id == MINIC_EXPRESSION_INVALID ||
           enqueue_expression(validation, expression_id);
}

static bool enqueue_cleanup_expressions(MinicFunctionBodyValidation *validation,
                                        const MinicStatement *statement) {
    const MinicC0Program *program;
    MinicCleanupContextId current;

    if (validation == NULL || validation->program == NULL || statement == NULL) {
        return false;
    }
    program = validation->program;
    if (statement->cleanup_context > program->cleanup_context_count ||
        statement->cleanup_stop_context > program->cleanup_context_count ||
        !minic_c0_cleanup_context_reaches(
            program, statement->cleanup_context, statement->cleanup_stop_context) ||
        !claim_cleanup_context(validation, statement->cleanup_stop_context)) {
        return false;
    }
    current = statement->cleanup_context;
    while (current != statement->cleanup_stop_context) {
        const MinicCleanupContext *cleanup;

        if (!claim_cleanup_context(validation, current)) {
            return false;
        }
        cleanup = minic_c0_program_cleanup_context(program, current);
        if (cleanup == NULL || !enqueue_expression(validation, cleanup->cleanup_expression)) {
            return false;
        }
        current = cleanup->parent;
    }
    return true;
}

static bool enqueue_inline_asm_expressions(MinicFunctionBodyValidation *validation,
                                           const MinicStatement *statement) {
    const MinicC0Program *program;
    const MinicInlineAsm *inline_asm;
    size_t index;

    if (validation == NULL || validation->program == NULL || statement == NULL) {
        return false;
    }
    program = validation->program;
    if (!claim_inline_asm(validation, statement->inline_asm_id)) {
        return false;
    }
    inline_asm = &program->inline_asms[statement->inline_asm_id];
    if (!storage_shape_is_valid(
            inline_asm->outputs, inline_asm->output_count, inline_asm->output_capacity) ||
        !storage_shape_is_valid(
            inline_asm->inputs, inline_asm->input_count, inline_asm->input_capacity) ||
        !storage_shape_is_valid(
            inline_asm->labels, inline_asm->label_count, inline_asm->label_capacity)) {
        return false;
    }
    for (index = 0U; index < inline_asm->output_count; ++index) {
        if (!enqueue_expression(validation, inline_asm->outputs[index].expression)) {
            return false;
        }
    }
    for (index = 0U; index < inline_asm->input_count; ++index) {
        if (!enqueue_expression(validation, inline_asm->inputs[index].expression)) {
            return false;
        }
    }
    return true;
}

static bool process_statement(MinicFunctionBodyValidation *validation,
                              const MinicStatement *statement) {
    if (validation == NULL || statement == NULL ||
        !enqueue_optional_expression(validation, statement->target_expression) ||
        !enqueue_optional_expression(validation, statement->expression) ||
        !enqueue_cleanup_expressions(validation, statement)) {
        return false;
    }
    if (statement->kind == MINIC_STATEMENT_INLINE_ASM &&
        !enqueue_inline_asm_expressions(validation, statement)) {
        return false;
    }

    switch (statement->kind) {
    case MINIC_STATEMENT_IF:
        return claim_block(validation, statement->then_block) &&
               (statement->else_block == MINIC_BLOCK_INVALID ||
                claim_block(validation, statement->else_block));
    case MINIC_STATEMENT_WHILE:
    case MINIC_STATEMENT_SWITCH:
        return claim_block(validation, statement->then_block);
    case MINIC_STATEMENT_ASSIGN:
    case MINIC_STATEMENT_RECORD_COPY:
    case MINIC_STATEMENT_XOR_ASSIGN:
    case MINIC_STATEMENT_EXPRESSION:
    case MINIC_STATEMENT_INLINE_ASM:
    case MINIC_STATEMENT_RETURN:
    case MINIC_STATEMENT_BREAK:
    case MINIC_STATEMENT_GOTO:
    case MINIC_STATEMENT_LABEL:
    case MINIC_STATEMENT_CASE:
    case MINIC_STATEMENT_DEFAULT:
        return true;
    }
    return false;
}

static bool process_block(MinicFunctionBodyValidation *validation, MinicBlockId block_id) {
    const MinicBlock *block;
    size_t index;

    if (validation == NULL || validation->program == NULL) {
        return false;
    }
    block = minic_c0_program_block(validation->program, block_id);
    if (block == NULL || !storage_shape_is_valid(block->statements,
                                                 block->statement_count,
                                                 block->statement_capacity)) {
        return false;
    }
    for (index = 0U; index < block->statement_count; ++index) {
        const MinicStatement *statement;
        MinicStatementId statement_id;

        statement_id = block->statements[index];
        if (!claim_statement(validation, statement_id)) {
            return false;
        }
        statement = minic_c0_program_statement(validation->program, statement_id);
        if (statement == NULL || !process_statement(validation, statement)) {
            return false;
        }
    }
    return true;
}

static bool local_is_owned(const MinicFunctionBodyValidation *validation, MinicLocalId local_id) {
    return validation != NULL && validation->program != NULL &&
           local_id < validation->program->local_count &&
           validation->local_owners[local_id] == validation->function_id;
}

static bool process_expression(MinicFunctionBodyValidation *validation,
                               MinicExpressionId expression_id) {
    const MinicExpression *expression;

    if (validation == NULL || validation->program == NULL) {
        return false;
    }
    expression = minic_c0_program_expression(validation->program, expression_id);
    if (expression == NULL) {
        return false;
    }

    if (expression->kind == MINIC_EXPRESSION_LOCAL &&
        !local_is_owned(validation, expression->value.local_id)) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_COMPOUND_LITERAL &&
        (!local_is_owned(validation, expression->value.compound_literal.local_id) ||
         !claim_block(validation, expression->value.compound_literal.initializer_block))) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_STATEMENT &&
        !claim_block(validation, expression->value.statement_expression.block)) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_LABEL_ADDRESS &&
        expression->value.label_statement_id >= validation->program->statement_count) {
        return false;
    }

    return visit_expression_child_ids(expression, enqueue_expression_visitor, validation);
}

static bool validate_semantic_edges(const MinicFunctionBodyValidation *validation) {
    const MinicC0Program *program;
    size_t index;

    if (validation == NULL || validation->program == NULL) {
        return false;
    }
    program = validation->program;
    for (index = 0U; index < validation->statement_work_count; ++index) {
        const MinicStatement *statement;

        statement = minic_c0_program_statement(program, validation->statement_work[index]);
        if (statement == NULL) {
            return false;
        }
        if (statement->kind == MINIC_STATEMENT_GOTO &&
            (statement->target_statement >= program->statement_count ||
             validation->statement_owners[statement->target_statement] !=
                 validation->function_id)) {
            return false;
        }
        if (statement->kind == MINIC_STATEMENT_INLINE_ASM) {
            const MinicInlineAsm *inline_asm;
            size_t label_index;

            if (statement->inline_asm_id >= program->inline_asm_count ||
                validation->inline_asm_owners[statement->inline_asm_id] !=
                    validation->function_id) {
                return false;
            }
            inline_asm = &program->inline_asms[statement->inline_asm_id];
            for (label_index = 0U; label_index < inline_asm->label_count; ++label_index) {
                MinicStatementId target;

                target = inline_asm->labels[label_index].target_statement;
                if (target >= program->statement_count ||
                    validation->statement_owners[target] != validation->function_id) {
                    return false;
                }
            }
        }
    }
    for (index = 0U; index < validation->expression_work_count; ++index) {
        const MinicExpression *expression;

        expression = minic_c0_program_expression(program, validation->expression_work[index]);
        if (expression == NULL) {
            return false;
        }
        if (expression->kind == MINIC_EXPRESSION_LABEL_ADDRESS &&
            validation->statement_owners[expression->value.label_statement_id] !=
                validation->function_id) {
            return false;
        }
    }
    return true;
}

static bool next_expression_generation(MinicFunctionBodyValidation *validation) {
    if (validation == NULL || validation->program == NULL) {
        return false;
    }
    if (validation->expression_generation == SIZE_MAX) {
        if (validation->program->expression_count != 0U) {
            (void)memset(validation->expression_generations,
                         0,
                         validation->program->expression_count *
                             sizeof(*validation->expression_generations));
        }
        validation->expression_generation = 1U;
        return true;
    }
    validation->expression_generation += 1U;
    return validation->expression_generation != 0U;
}

static bool validate_one_function(MinicFunctionBodyValidation *validation,
                                  MinicFunctionId function_id) {
    MinicFunctionBodyView view;

    if (validation == NULL || validation->program == NULL ||
        function_id == MINIC_FUNCTION_INVALID ||
        !minic_c0_function_body_view(validation->program, function_id, &view) ||
        !next_expression_generation(validation)) {
        return false;
    }
    validation->function_id = function_id;
    validation->block_work_count = 0U;
    validation->block_work_cursor = 0U;
    validation->statement_work_count = 0U;
    validation->expression_work_count = 0U;
    validation->expression_work_cursor = 0U;

    if (!claim_block(validation, minic_c0_function_body_root_block(&view))) {
        return false;
    }
    while (validation->block_work_cursor < validation->block_work_count ||
           validation->expression_work_cursor < validation->expression_work_count) {
        if (validation->block_work_cursor < validation->block_work_count) {
            MinicBlockId block_id;

            block_id = validation->block_work[validation->block_work_cursor++];
            if (!process_block(validation, block_id)) {
                return false;
            }
        } else {
            MinicExpressionId expression_id;

            expression_id = validation->expression_work[validation->expression_work_cursor++];
            if (!process_expression(validation, expression_id)) {
                return false;
            }
        }
    }
    return validate_semantic_edges(validation);
}

bool minic_c0_program_validate_function_body_ownership(const MinicC0Program *program) {
    MinicFunctionBodyValidation validation;
    size_t function_index;
    bool success;

    if (!initialize_validation(program, &validation)) {
        return false;
    }
    if (!assign_local_owners(&validation)) {
        destroy_validation(&validation);
        return false;
    }

    success = true;
    for (function_index = 0U; success && function_index < program->function_count;
         ++function_index) {
        if (!program->functions[function_index].is_defined) {
            continue;
        }
        success = validate_one_function(&validation, function_index);
    }
    destroy_validation(&validation);
    return success;
}
