#include "compiler/inline_specialize.h"
#include "compiler/inline_specialize_internal.h"
#include "core/opt_pure_statement.h"
#include "frontend/const_eval.h"
#include "frontend/function_body.h"
#include "target/backend.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Compiler AST pass: integer constant specialization & AST clone creation.
 * Function bodies remain from the validated monolithic optimizer. */
#define MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT 2048U
/* A small, separately budgeted subset of TU-local non-inline functions needs
 * caller constants to discharge GNU compile-time guards (e.g. BUILD_BUG_ON).
 * Exhaustion is only a missed optimization, never an invalid C program. */
#define MINIC_STATIC_INTEGER_SPECIALIZATION_LIMIT 64U

typedef struct MinicInlinePureTruth {
    const MinicC0Program *program;
    const MinicTargetInfo *target;
} MinicInlinePureTruth;

static bool minic_inline_pure_constant_truth(void *opaque,
                                             MinicExpressionId condition_id,
                                             bool *truth) {
    const MinicInlinePureTruth *context = (const MinicInlinePureTruth *)opaque;
    MinicConstValue constant;
    bool zero;
    if (context == NULL || truth == NULL ||
        !minic_const_eval_integer(context->program, context->target,
                                  condition_id, &constant) ||
        !minic_const_value_is_zero(context->program, context->target,
                                   &constant, &zero)) return false;
    *truth = !zero;
    return true;
}

static bool minic_inline_pure_statement_constant(const MinicC0Program *program,
                                                 const MinicTargetInfo *target,
                                                 MinicCoreReentryIndex *reentry_index,
                                                 MinicExpressionId expression_id,
                                                 MinicConstValue *value) {
    const MinicExpression *expression;
    MinicInlinePureTruth truth_context;
    if (program == NULL || target == NULL || value == NULL) return false;
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL || expression->kind != MINIC_EXPRESSION_STATEMENT ||
        expression->value.statement_expression.result == MINIC_EXPRESSION_INVALID ||
        expression->value.statement_expression.block == MINIC_BLOCK_INVALID)
        return false;
    truth_context.program = program;
    truth_context.target = target;
    if (!minic_opt_pure_statement_block(program, target, reentry_index,
            expression->value.statement_expression.block, &truth_context,
            minic_inline_pure_constant_truth, 0U)) return false;
    return minic_const_eval_integer(program, target,
                expression->value.statement_expression.result, value);
}

static bool minic_static_integer_specialization_eligible(const MinicFunction *source) {
    return source != NULL && source->is_defined && source->is_internal &&
           !source->is_inline && !source->is_weak && !source->is_constructor &&
           !source->force_emit && source->assembler_name == NULL &&
           !source->is_variadic && source->alias_target == MINIC_FUNCTION_INVALID &&
           source->local_count <= 256U;
}

static bool minic_static_integer_claim_block(
    MinicBlockId block_id, MinicFunctionId owner,
    MinicFunctionId *block_owners, MinicBlockId *queue,
    size_t block_count, size_t *queue_count) {
    if (block_id == MINIC_BLOCK_INVALID) {
        return true;
    }
    if (block_id >= block_count || owner == MINIC_FUNCTION_INVALID) {
        return false;
    }
    if (block_owners[block_id] == MINIC_FUNCTION_INVALID) {
        if (*queue_count >= block_count) {
            return false;
        }
        block_owners[block_id] = owner;
        queue[(*queue_count)++] = block_id;
    }
    return block_owners[block_id] == owner;
}

/* A static function containing computed-goto label-address expressions must
 * retain its original source-label aliases. A clone deliberately suppresses
 * those aliases, so do not specialize such a source in the first place. */
static unsigned char *minic_static_integer_blocked_sources(
    const MinicC0Program *program, const MinicTargetInfo *target) {
    unsigned char *blocked;
    const MinicBackend *backend;
    unsigned char *inline_has_external_guard = NULL;
    unsigned char *static_calls_guarded_inline = NULL;
    MinicFunctionId *owners = NULL;
    MinicFunctionId *block_owners = NULL;
    MinicBlockId *block_queue = NULL;
    size_t block_queue_count = 0U;
    size_t block_cursor = 0U;
    size_t index;

    if (program == NULL || target == NULL) {
        return NULL;
    }
    backend = minic_backend_for_target(target);
    if (backend == NULL) {
        return NULL;
    }
    blocked = (unsigned char *)calloc(
        program->function_count == 0U ? 1U : program->function_count,
        sizeof(*blocked));
    if (blocked == NULL) {
        return NULL;
    }
    inline_has_external_guard = (unsigned char *)calloc(
        program->function_count == 0U ? 1U : program->function_count, 1U);
    static_calls_guarded_inline = (unsigned char *)calloc(
        program->function_count == 0U ? 1U : program->function_count, 1U);
    if (inline_has_external_guard == NULL ||
        static_calls_guarded_inline == NULL) {
        goto blocked_error;
    }
    if (program->expression_count != 0U) {
        if (program->expression_count > SIZE_MAX / sizeof(*owners)) {
            goto blocked_error;
        }
        owners = (MinicFunctionId *)malloc(program->expression_count * sizeof(*owners));
        if (owners == NULL || !minic_perf_program_build_expression_source_owners(
                program, program->function_count, owners, program->expression_count)) {
            goto blocked_error;
        }
        for (index = 0U; index < program->expression_count; ++index) {
            if (program->expressions[index].kind == MINIC_EXPRESSION_LABEL_ADDRESS) {
                if (owners[index] < program->function_count) {
                    blocked[owners[index]] = 1U;
                } else {
                    size_t object_index;
                    bool relocated_label_owned = false;
                    for (object_index = 0U;
                         object_index < program->global_object_count;
                         ++object_index) {
                        const MinicGlobalObject *object =
                            &program->global_objects[object_index];
                        size_t reloc_index;
                        for (reloc_index = 0U;
                             reloc_index < object->relocation_count;
                             ++reloc_index) {
                            const MinicGlobalRelocation *reloc =
                                &object->relocations[reloc_index];
                            if (reloc->target_kind == MINIC_GLOBAL_RELOCATION_LABEL &&
                                reloc->target_id ==
                                    program->expressions[index].value.label_statement_id &&
                                object->owner_function < program->function_count) {
                                blocked[object->owner_function] = 1U;
                                relocated_label_owned = true;
                            }
                        }
                    }
                    if (!relocated_label_owned) {
                        goto blocked_error;
                    }
                }
            }
        }
        /* Do not clone arbitrary static utility functions merely because an
         * integer actual happens to be constant. Restrict this extension to
         * the observable GNU case: a static source calls an inline helper
         * whose body has an external diagnostic/guard path. There
         * is no source-symbol spelling test and no target-specific rule. */
        for (index = 0U; index < program->expression_count; ++index) {
            const MinicExpression *call = &program->expressions[index];
            MinicFunctionId callee_id;
            if (call->kind != MINIC_EXPRESSION_CALL ||
                owners[index] >= program->function_count) {
                continue;
            }
            callee_id = call->value.call.function_id;
            if (callee_id < program->function_count &&
                !program->functions[callee_id].is_defined) {
                inline_has_external_guard[owners[index]] = 1U;
                /* GNU __attribute__((error)) is checked after optimization.
                 * An ordinary TU-local (non-inline) helper may contain that
                 * guard directly, as with a constant FIELD_GET mask.  Allow
                 * callsite integer specialization to discharge it, without
                 * cloning arbitrary static functions just because they call
                 * an unrelated external symbol. */
                if (program->functions[callee_id].error_message != NULL) {
                    static_calls_guarded_inline[owners[index]] = 1U;
                }
            }
        }
        for (index = 0U; index < program->expression_count; ++index) {
            const MinicExpression *call = &program->expressions[index];
            MinicFunctionId callee_id;
            if (call->kind != MINIC_EXPRESSION_CALL ||
                owners[index] >= program->function_count) {
                continue;
            }
            callee_id = call->value.call.function_id;
            if (callee_id < program->function_count &&
                program->functions[callee_id].is_defined &&
                program->functions[callee_id].is_internal &&
                program->functions[callee_id].is_inline &&
                inline_has_external_guard[callee_id]) {
                static_calls_guarded_inline[owners[index]] = 1U;
            }
        }
        for (index = 0U; index < program->function_count; ++index) {
            if (!static_calls_guarded_inline[index]) {
                blocked[index] = 1U;
            }
        }
    }
    if (program->block_count != 0U) {
        if (program->block_count > SIZE_MAX / sizeof(*block_owners) ||
            program->block_count > SIZE_MAX / sizeof(*block_queue)) {
            goto blocked_error;
        }
        block_owners = (MinicFunctionId *)malloc(
            program->block_count * sizeof(*block_owners));
        block_queue = (MinicBlockId *)malloc(
            program->block_count * sizeof(*block_queue));
        if (block_owners == NULL || block_queue == NULL) {
            goto blocked_error;
        }
        for (index = 0U; index < program->block_count; ++index) {
            block_owners[index] = MINIC_FUNCTION_INVALID;
        }
        for (index = 0U; index < program->function_count; ++index) {
            const MinicFunction *function = &program->functions[index];
            if (function->is_defined && !function->is_integer_specialization &&
                !minic_static_integer_claim_block(
                    function->body_block, (MinicFunctionId)index,
                    block_owners, block_queue, program->block_count,
                    &block_queue_count)) {
                goto blocked_error;
            }
        }
        for (index = 0U; index < program->expression_count; ++index) {
            const MinicExpression *expression = &program->expressions[index];
            if (expression->kind == MINIC_EXPRESSION_STATEMENT ||
                expression->kind == MINIC_EXPRESSION_COMPOUND_LITERAL) {
                MinicBlockId nested_block =
                    expression->kind == MINIC_EXPRESSION_STATEMENT
                        ? expression->value.statement_expression.block
                        : expression->value.compound_literal.initializer_block;
                /* The source-owner walk intentionally leaves parse-only,
                 * unreachable statement expressions unowned.  Their nested
                 * blocks cannot affect an emitted function, so they must not
                 * invalidate source-safety checking for the entire TU. */
                if (owners != NULL && owners[index] == MINIC_FUNCTION_INVALID) {
                    continue;
                }
                if (owners == NULL || owners[index] >= program->function_count ||
                    !minic_static_integer_claim_block(
                        nested_block,
                        owners[index], block_owners, block_queue,
                        program->block_count, &block_queue_count)) {
                    goto blocked_error;
                }
            }
        }
        while (block_cursor < block_queue_count) {
            MinicBlockId id = block_queue[block_cursor++];
            const MinicBlock *block = &program->blocks[id];
            MinicFunctionId owner = block_owners[id];
            size_t statement_index;
            for (statement_index = 0U; statement_index < block->statement_count;
                 ++statement_index) {
                const MinicStatement *statement = minic_c0_program_statement(
                    program, block->statements[statement_index]);
                if (statement == NULL) {
                    goto blocked_error;
                }
                if (statement->kind == MINIC_STATEMENT_INLINE_ASM) {
                    const MinicInlineAsm *asm_source =
                        minic_c0_program_inline_asm(program, statement->inline_asm_id);
                    if (!minic_backend_clone_safe_opaque_asm(backend, asm_source)) {
                        blocked[owner] = 1U;
                    }
                }
                if (statement->kind == MINIC_STATEMENT_IF ||
                    statement->kind == MINIC_STATEMENT_WHILE ||
                    statement->kind == MINIC_STATEMENT_SWITCH) {
                    if (!minic_static_integer_claim_block(
                            statement->then_block, owner, block_owners,
                            block_queue, program->block_count, &block_queue_count) ||
                        (statement->kind == MINIC_STATEMENT_IF &&
                         !minic_static_integer_claim_block(
                            statement->else_block, owner, block_owners,
                            block_queue, program->block_count, &block_queue_count))) {
                        goto blocked_error;
                    }
                }
            }
        }
    }
    for (index = 0U; index < program->global_object_count; ++index) {
        const MinicGlobalObject *object = &program->global_objects[index];
        size_t relocation_index;
        for (relocation_index = 0U; relocation_index < object->relocation_count;
             ++relocation_index) {
            if (object->relocations[relocation_index].target_kind ==
                    MINIC_GLOBAL_RELOCATION_LABEL &&
                object->owner_function < program->function_count) {
                blocked[object->owner_function] = 1U;
            }
        }
    }
    free(owners);
    free(block_owners);
    free(block_queue);
    free(inline_has_external_guard);
    free(static_calls_guarded_inline);
    return blocked;

blocked_error:
    free(owners);
    free(block_owners);
    free(block_queue);
    free(inline_has_external_guard);
    free(static_calls_guarded_inline);
    free(blocked);
    return NULL;
}

static bool minic_inline_integer_specialization_matches(
    const MinicFunction *candidate,
    const MinicFunction *source,
    const MinicFunction *caller,
    MinicFunctionId source_id,
    const bool *known,
    const uint64_t *bits,
    size_t parameter_count) {
    size_t i;
    if (candidate == NULL || !candidate->is_integer_specialization ||
        candidate->specialization_source != source_id ||
        candidate->parameter_count != parameter_count ||
        !minic_inline_specialization_section_matches(candidate, source, caller)) {
        return false;
    }
    for (i = 0U; i < parameter_count; ++i) {
        if (candidate->specialization_integer_known[i] != known[i] ||
            (known[i] && candidate->specialization_integer_bits[i] != bits[i])) {
            return false;
        }
    }
    return true;
}

bool minic_inline_specialization_section_matches(
    const MinicFunction *candidate,
    const MinicFunction *source,
    const MinicFunction *caller) {
    const MinicFunction *owner;
    const char *wanted;
    size_t length;

    if (candidate == NULL || source == NULL) return false;
    owner = source->section_name != NULL ? source : caller;
    wanted = owner != NULL ? owner->section_name : NULL;
    length = wanted != NULL ? owner->section_name_length : 0U;
    if ((candidate->section_name == NULL) != (wanted == NULL) ||
        candidate->section_name_length != length) return false;
    return wanted == NULL || memcmp(candidate->section_name, wanted, length) == 0;
}

bool minic_inline_section_owner_index_required(const MinicC0Program *program) {
    size_t index;
    if (program == NULL) return false;
    for (index = 0U; index < program->function_count; ++index)
        if (program->functions[index].section_name != NULL) return true;
    return false;
}

/* An unresolved __builtin_constant_p(expr) is evaluated later, from the
 * current caller's Core facts.  A conservative zero-result clone is useful
 * for GNU inline helpers whose fallback is legal only when expr is not
 * compile-time constant, but this AST call must NOT be rewritten to it:
 * another caller specialization can still prove the query true.  The
 * existing Core callee selector chooses this clone only when it proves 0. */
static bool minic_inline_integer_unresolved_constant_query(
    const MinicC0Program *program, MinicExpressionId expression_id,
    unsigned int depth) {
    const MinicExpression *expression;
    if (program == NULL || depth > 8U) return false;
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL || !minic_type_is_integer(expression->type))
        return false;
    if (expression->kind == MINIC_EXPRESSION_BUILTIN_UNARY)
        return expression->value.builtin_unary.operator_kind ==
            MINIC_BUILTIN_UNARY_CONSTANT_P;
    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION)
        return minic_inline_integer_unresolved_constant_query(
            program, expression->value.unary.operand, depth + 1U);
    return false;
}

bool minic_add_inline_integer_specialization(
    MinicC0Program *program,
    MinicFunctionId source_id,
    const MinicFunction *caller,
    const bool *known,
    const uint64_t *bits,
    size_t ordinal,
    MinicFunctionId *specialized_id) {
    MinicFunction source;
    MinicFunction *specialized;
    const char *section;
    size_t section_length;
    char name[96];
    int name_length;
    size_t i;

    if (program == NULL || source_id >= program->function_count ||
        specialized_id == NULL) {
        return false;
    }
    source = program->functions[source_id];
    section = source.section_name != NULL ? source.section_name :
        caller != NULL ? caller->section_name : NULL;
    section_length = source.section_name != NULL ? source.section_name_length :
        caller != NULL && caller->section_name != NULL ? caller->section_name_length : 0U;
    if (!source.is_defined || !source.is_internal ||
        (!source.is_inline && !minic_static_integer_specialization_eligible(&source)) ||
        source.is_variadic || source.alias_target != MINIC_FUNCTION_INVALID) {
        return false;
    }
    /* A clone owns a new FunctionId and specialization facts, not a copied
     * FunctionBody. Several clones intentionally share body_block/local IDs;
     * an ExpressionId inside that body cannot identify a particular caller's
     * CALL instance (the separate MC-006 provenance problem). */
    name_length = snprintf(name, sizeof(name), "__minic_inline_spec_%zu_%zu",
                           (size_t)source_id, ordinal);
    if (name_length <= 0 || (size_t)name_length >= sizeof(name) ||
        !minic_c0_program_add_function(program, name, (size_t)name_length,
                                       source.local_begin, source.local_count,
                                       source.body_block, specialized_id) ||
        !minic_c0_program_set_function_signature(program, *specialized_id,
                                                 source.return_type,
                                                 source.parameter_types,
                                                 source.parameter_count) ||
        !minic_c0_program_set_function_calling_convention(
            program, *specialized_id, source.calling_convention) ||
        !minic_c0_program_set_function_internal(program, *specialized_id, true) ||
        !minic_c0_program_set_function_inline(program, *specialized_id, source.is_inline) ||
        (source.is_noreturn &&
         !minic_c0_program_set_function_noreturn(program, *specialized_id, true)) ||
        (section != NULL &&
         !minic_c0_program_set_function_section(program, *specialized_id,
                                                section, section_length))) {
        return false;
    }
    specialized = &program->functions[*specialized_id];
    specialized->visibility = source.visibility;
    specialized->explicit_alignment = source.explicit_alignment;
    specialized->specialization_source = source_id;
    specialized->is_integer_specialization = true;
    for (i = 0U; i < source.parameter_count; ++i) {
        specialized->specialization_integer_known[i] = known[i];
        specialized->specialization_integer_bits[i] = bits[i];
    }
    return true;
}



static bool minic_inline_specialization_argument_value(
    const MinicC0Program *program,
    const MinicTargetInfo *target,
    const MinicFunction *caller,
    MinicExpressionId expression_id,
    MinicType target_type,
    MinicConstValue *value,
    bool *used_caller_fact) {
    const MinicExpression *expression;
    MinicConstValue operand_value;
    MinicConstValue constant_value;

    if (program == NULL || target == NULL || caller == NULL || value == NULL ||
        used_caller_fact == NULL || !caller->is_integer_specialization ||
        caller->specialization_source >= program->function_count ||
        !minic_type_is_integer(target_type)) {
        return false;
    }
    *used_caller_fact = false;
    if (minic_const_eval_integer(program, target, expression_id, &constant_value) &&
        minic_const_value_convert_integer(
            program, target, &constant_value, target_type, value)) {
        return true;
    }
    expression = minic_c0_program_expression(program, expression_id);
    if (expression == NULL || !minic_type_is_integer(expression->type)) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION ||
        expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
        bool operand_used_caller_fact = false;
        if (!minic_inline_specialization_argument_value(
                program,
                target,
                caller,
                expression->value.unary.operand,
                expression->type,
                &operand_value,
                &operand_used_caller_fact) ||
            !minic_const_value_convert_integer(
                program, target, &operand_value, target_type, value)) {
            return false;
        }
        *used_caller_fact = operand_used_caller_fact;
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        const MinicFunction *source;
        MinicLocalId local_id;
        size_t parameter_index;

        source = &program->functions[caller->specialization_source];
        local_id = expression->value.local_id;
        if (local_id < source->local_begin) {
            return false;
        }
        parameter_index = local_id - source->local_begin;
        if (parameter_index >= source->parameter_count ||
            parameter_index >= MINIC_MAX_FUNCTION_PARAMETERS ||
            !caller->specialization_integer_known[parameter_index] ||
            !minic_type_is_integer(source->parameter_types[parameter_index])) {
            return false;
        }
        constant_value.type = source->parameter_types[parameter_index];
        constant_value.bits = caller->specialization_integer_bits[parameter_index];
        if (!minic_const_value_convert_integer(
                program, target, &constant_value, target_type, value)) {
            return false;
        }
        *used_caller_fact = true;
        return true;
    }
    return false;
}

bool minic_specialize_inline_integer_calls(MinicC0Program *program,
                                                  const MinicTargetInfo *target) {
    size_t expression_index;
    size_t original_function_count;
    size_t specialization_count = 0U;
    size_t static_specialization_count = 0U;
    unsigned char *static_blocked_sources = NULL;
    bool static_sources_checked = false;
    MinicCoreReentryIndex *pure_guard_reentry_index = NULL;
    bool pure_guard_index_attempted = false;
    MinicFunctionId *initial_expression_owners = NULL;
    bool initial_owner_index_ready = false;

    if (program == NULL || target == NULL) {
        return false;
    }
    original_function_count = program->function_count;
    if (minic_inline_section_owner_index_required(program) &&
        program->expression_count != 0U &&
        program->expression_count <= SIZE_MAX / sizeof(*initial_expression_owners)) {
        initial_expression_owners = malloc(program->expression_count *
                                           sizeof(*initial_expression_owners));
        if (initial_expression_owners != NULL)
            initial_owner_index_ready = minic_perf_program_build_expression_source_owners(
                program, original_function_count, initial_expression_owners,
                program->expression_count);
    }
    if (minic_inline_section_owner_index_required(program) &&
        program->expression_count != 0U && !initial_owner_index_ready) {
        free(initial_expression_owners);
        (void)fprintf(stderr, "MINIC_INLINE_SECTION_OWNER_INDEX=FAIL integer\n");
        return false;
    }
    for (expression_index = 0U; expression_index < program->expression_count;
         ++expression_index) {
        MinicExpression *expression = &program->expressions[expression_index];
        MinicFunctionId source_id;
        MinicFunctionId specialized_id = MINIC_FUNCTION_INVALID;
        MinicFunction source;
        const MinicFunction *caller = NULL;
        bool known[MINIC_MAX_FUNCTION_PARAMETERS];
        uint64_t bits[MINIC_MAX_FUNCTION_PARAMETERS];
        size_t i;
        size_t candidate_index;
        bool has_integer_constant = false;
        bool unresolved_constant_query = false;

        if (expression->kind != MINIC_EXPRESSION_CALL ||
            expression->value.call.function_id == MINIC_FUNCTION_INVALID) {
            continue;
        }
        source_id = expression->value.call.function_id;
        if (source_id >= original_function_count) {
            continue;
        }
        source = program->functions[source_id];
        if (initial_owner_index_ready &&
            initial_expression_owners[expression_index] < original_function_count)
            caller = &program->functions[initial_expression_owners[expression_index]];
        if (!source.is_defined || !source.is_internal ||
            (!source.is_inline && !minic_static_integer_specialization_eligible(&source)) ||
            !source.is_referenced || source.is_variadic ||
            source.alias_target != MINIC_FUNCTION_INVALID || source.parameter_count == 0U ||
            source.parameter_count != expression->value.call.argument_count) {
            continue;
        }
        if (!source.is_inline) {
            if (!static_sources_checked) {
                static_blocked_sources = minic_static_integer_blocked_sources(
                    program, target);
                static_sources_checked = true;
            }
            if (static_blocked_sources == NULL) {
                continue;
            }
            if (static_blocked_sources[source_id] == 0U) {
                static_blocked_sources[source_id] =
                    minic_inline_source_has_opaque_asm_reference(program, &source)
                        ? 1U : 2U;
            }
            if (static_blocked_sources[source_id] == 1U) {
                continue;
            }
        }
        (void)memset(known, 0, sizeof(known));
        (void)memset(bits, 0, sizeof(bits));
        for (i = 0U; i < source.parameter_count; ++i) {
            MinicConstValue argument_value;
            MinicConstValue converted_value;
            MinicExpressionId argument_id = minic_c0_call_argument(expression, i);
            const MinicExpression *argument_expression = minic_c0_program_expression(
                program, argument_id);
            bool argument_known;
            if (!minic_type_is_integer(source.parameter_types[i])) {
                continue;
            }
            argument_known = minic_const_eval_integer(program, target, argument_id,
                                                      &argument_value);
            if (!argument_known && argument_expression != NULL &&
                argument_expression->kind == MINIC_EXPRESSION_STATEMENT) {
                if (!pure_guard_index_attempted) {
                    pure_guard_index_attempted = true;
                    pure_guard_reentry_index = minic_core_reentry_index_create(program);
                }
                argument_known = minic_inline_pure_statement_constant(
                    program, target, pure_guard_reentry_index,
                    argument_id, &argument_value);
            }
            if (argument_known &&
                minic_const_value_convert_integer(program, target,
                                                  &argument_value,
                                                  source.parameter_types[i],
                                                  &converted_value)) {
                known[i] = true;
                bits[i] = converted_value.bits;
                has_integer_constant = true;
            } else if (minic_inline_integer_unresolved_constant_query(
                           program, argument_id, 0U)) {
                /* This speculative 0 must be revalidated in Core at the
                 * real callsite; never replace the shared source CALL. */
                known[i] = true;
                bits[i] = 0U;
                unresolved_constant_query = true;
            }
        }
        if (!has_integer_constant && !unresolved_constant_query) {
            continue;
        }
        for (candidate_index = original_function_count;
             candidate_index < program->function_count; ++candidate_index) {
            if (minic_inline_integer_specialization_matches(
                    &program->functions[candidate_index], &source, caller,
                    source_id, known, bits,
                    source.parameter_count)) {
                specialized_id = candidate_index;
                break;
            }
        }
        if (specialized_id == MINIC_FUNCTION_INVALID) {
            if (!source.is_inline &&
                (static_specialization_count >= MINIC_STATIC_INTEGER_SPECIALIZATION_LIMIT ||
                 specialization_count >= MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT)) {
                continue;
            }
            if (specialization_count >= MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT &&
                unresolved_constant_query) {
                /* Optional candidate-only guard; a missed optimization is
                 * never permission to reject otherwise valid C input. */
                continue;
            }
            if (specialization_count >= MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT) {
                /* FIXME(MC-043): this is an optimization budget, but the
                 * current caller treats exhaustion as a fatal TU error even
                 * when the original unspecialized call has valid semantics. */
                (void)fprintf(stderr,
                              "MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT_HIT count=%zu limit=%u functions=%zu source=%zu\n",
                              specialization_count,
                              (unsigned int)MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT,
                              program->function_count,
                              (size_t)source_id);
                minic_core_reentry_index_destroy(pure_guard_reentry_index);
                free(static_blocked_sources);
                free(initial_expression_owners);
                return false;
            }
            if (!minic_add_inline_integer_specialization(program, source_id, caller,
                                                         known, bits,
                                                         specialization_count,
                                                         &specialized_id)) {
                (void)fprintf(stderr,
                              "MINIC_INLINE_INTEGER_SPECIALIZATION_ADD_FAILED count=%zu functions=%zu source=%zu\n",
                              specialization_count,
                              program->function_count,
                              (size_t)source_id);
                minic_core_reentry_index_destroy(pure_guard_reentry_index);
                free(static_blocked_sources);
                free(initial_expression_owners);
                return false;
            }
            specialization_count += 1U;
            if (!source.is_inline) {
                static_specialization_count += 1U;
            }
        }
        if (!unresolved_constant_query)
            program->expressions[expression_index].value.call.function_id = specialized_id;
    }
    free(static_blocked_sources);
    free(initial_expression_owners);

    {
        size_t caller_index;
        size_t refinement_count = 0U;
        size_t *calls = NULL, call_count = 0U, pos;
        const size_t expression_limit = program->expression_count;
        /* Reuse the canonical source-FunctionBody ownership index.  Only
         * uniquely-owned calls from another source may be skipped.  Shared,
         * ambiguous, and unowned expressions retain the original path.
         * Allocation/validation failure deliberately falls back to all calls. */
        MinicFunctionId *integer_expression_owners = NULL;
        bool integer_owner_index_ready = false;
        size_t integer_owner_skipped = 0U;
        size_t integer_call_candidates = 0U;
        if (expression_limit != 0U &&
            expression_limit <= SIZE_MAX / sizeof(*integer_expression_owners)) {
            integer_expression_owners =
                (MinicFunctionId *)malloc(expression_limit *
                                          sizeof(*integer_expression_owners));
            if (integer_expression_owners != NULL) {
                integer_owner_index_ready =
                    minic_perf_program_build_expression_source_owners(
                        program, original_function_count,
                        integer_expression_owners, expression_limit);
                if (!integer_owner_index_ready) {
                    free(integer_expression_owners);
                    integer_expression_owners = NULL;
                }
            }
        }
        if (expression_limit > 0U &&
            expression_limit <= SIZE_MAX / sizeof(*calls)) {
            calls = malloc(expression_limit * sizeof(*calls));
            if (calls != NULL) {
                for (pos = 0U; pos < expression_limit; ++pos)
                    if (program->expressions[pos].kind == MINIC_EXPRESSION_CALL)
                        calls[call_count++] = pos;
            }
        }

        for (caller_index = original_function_count;
             caller_index < program->function_count;
             ++caller_index) {
            const MinicFunction caller = program->functions[caller_index];
            size_t nested_scan_index;

            if (!caller.is_integer_specialization ||
                caller.specialization_source >= original_function_count) {
                continue;
            }
            for (nested_scan_index = 0U;
                 nested_scan_index < ((calls != NULL &&
                    program->expression_count == expression_limit)
                        ? call_count : program->expression_count);
                 ++nested_scan_index) {
                size_t nested_expression_index =
                    (calls != NULL && program->expression_count == expression_limit)
                    ? calls[nested_scan_index] : nested_scan_index;
                const MinicExpression *nested = &program->expressions[nested_expression_index];
                MinicFunctionId current_callee_id;

                integer_call_candidates += 1U;
                if (integer_owner_index_ready &&
                    nested_expression_index < expression_limit) {
                    MinicFunctionId expression_owner =
                        integer_expression_owners[nested_expression_index];
                    if (expression_owner != MINIC_FUNCTION_INVALID &&
                        expression_owner < original_function_count &&
                        expression_owner != caller.specialization_source) {
                        integer_owner_skipped += 1U;
                        continue;
                    }
                }
                MinicFunctionId nested_source_id;
                MinicFunctionId refined_id = MINIC_FUNCTION_INVALID;
                MinicFunction current_callee;
                MinicFunction nested_source;
                bool nested_known[MINIC_MAX_FUNCTION_PARAMETERS];
                uint64_t nested_bits[MINIC_MAX_FUNCTION_PARAMETERS];
                bool used_caller_fact = false;
                bool adds_fact = false;
                bool needs_section_clone;
                size_t parameter_index;
                size_t nested_candidate_index;

                if (nested->kind != MINIC_EXPRESSION_CALL ||
                    nested->value.call.function_id == MINIC_FUNCTION_INVALID) {
                    continue;
                }
                current_callee_id = nested->value.call.function_id;
                if (current_callee_id >= program->function_count) {
                    continue;
                }
                current_callee = program->functions[current_callee_id];
                if (current_callee.is_integer_specialization) {
                    if (current_callee.specialization_source >= original_function_count) {
                        continue;
                    }
                    nested_source_id = current_callee.specialization_source;
                } else {
                    if (current_callee_id >= original_function_count) {
                        continue;
                    }
                    nested_source_id = current_callee_id;
                }
                nested_source = program->functions[nested_source_id];
                if (!nested_source.is_defined || !nested_source.is_internal ||
                    !nested_source.is_inline || nested_source.is_variadic ||
                    nested_source.alias_target != MINIC_FUNCTION_INVALID ||
                    nested_source.parameter_count == 0U ||
                    nested_source.parameter_count != nested->value.call.argument_count) {
                    continue;
                }
                needs_section_clone = current_callee.is_integer_specialization &&
                    !minic_inline_specialization_section_matches(
                        &current_callee, &nested_source, &caller);
                (void)memset(nested_known, 0, sizeof(nested_known));
                (void)memset(nested_bits, 0, sizeof(nested_bits));
                for (parameter_index = 0U;
                     parameter_index < nested_source.parameter_count;
                     ++parameter_index) {
                    MinicConstValue argument_value;
                    bool argument_used_caller_fact = false;

                    if (!minic_type_is_integer(nested_source.parameter_types[parameter_index]) ||
                        !minic_inline_specialization_argument_value(
                            program,
                            target,
                            &caller,
                            minic_c0_call_argument(nested, parameter_index),
                            nested_source.parameter_types[parameter_index],
                            &argument_value,
                            &argument_used_caller_fact)) {
                        continue;
                    }
                    nested_known[parameter_index] = true;
                    nested_bits[parameter_index] = argument_value.bits;
                    used_caller_fact = used_caller_fact || argument_used_caller_fact;
                    if (!current_callee.is_integer_specialization ||
                        !current_callee.specialization_integer_known[parameter_index] ||
                        current_callee.specialization_integer_bits[parameter_index] !=
                            argument_value.bits) {
                        adds_fact = true;
                    }
                }
                /* The shared source AST can already name a clone made for a
                 * different section. Recreate the identical known-value facts
                 * in this specialized caller's section even when no argument
                 * gains a new transitive fact; otherwise Core falls back to an
                 * unlowerable generic Linux inline helper. */
                if (needs_section_clone) {
                    for (parameter_index = 0U;
                         parameter_index < nested_source.parameter_count;
                         ++parameter_index) {
                        if (!nested_known[parameter_index] &&
                            current_callee.specialization_integer_known[parameter_index]) {
                            nested_known[parameter_index] = true;
                            nested_bits[parameter_index] =
                                current_callee.specialization_integer_bits[parameter_index];
                        }
                    }
                }
                if ((!used_caller_fact || !adds_fact) && !needs_section_clone) {
                    continue;
                }
                for (nested_candidate_index = original_function_count;
                     nested_candidate_index < program->function_count;
                     ++nested_candidate_index) {
                    if (minic_inline_integer_specialization_matches(
                            &program->functions[nested_candidate_index],
                            &nested_source, &caller,
                            nested_source_id,
                            nested_known,
                            nested_bits,
                            nested_source.parameter_count)) {
                        refined_id = nested_candidate_index;
                        break;
                    }
                }
                if (refined_id == MINIC_FUNCTION_INVALID) {
                    if (specialization_count >= MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT) {
                        (void)fprintf(stderr,
                                      "MINIC_INLINE_INTEGER_SPECIALIZATION_TRANSITIVE_LIMIT_HIT count=%zu limit=%u functions=%zu source=%zu\n",
                                      specialization_count,
                                      (unsigned int)MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT,
                                      program->function_count,
                                      (size_t)nested_source_id);
                        free(integer_expression_owners);
                        free(calls);
                        minic_core_reentry_index_destroy(pure_guard_reentry_index);
                        return false;
                    }
                    if (!minic_add_inline_integer_specialization(
                            program,
                            nested_source_id,
                            &caller,
                            nested_known,
                            nested_bits,
                            specialization_count,
                            &refined_id)) {
                        (void)fprintf(stderr,
                                      "MINIC_INLINE_INTEGER_SPECIALIZATION_TRANSITIVE_ADD_FAILED count=%zu functions=%zu source=%zu\n",
                                      specialization_count,
                                      program->function_count,
                                      (size_t)nested_source_id);
                        free(integer_expression_owners);
                        free(calls);
                        minic_core_reentry_index_destroy(pure_guard_reentry_index);
                        return false;
                    }
                    specialization_count += 1U;
                    refinement_count += 1U;
                }
            }
        }
        free(calls);
        free(integer_expression_owners);
        if (getenv("MINIC_INTEGER_OWNER_PERF") != NULL) {
            (void)fprintf(stderr,
                          "MINIC_INTEGER_OWNER_FILTER ready=%u "
                          "candidates=%zu skipped_other_owner=%zu "
                          "fallback=%u\n",
                          (unsigned int)integer_owner_index_ready,
                          integer_call_candidates,
                          integer_owner_skipped,
                          (unsigned int)!integer_owner_index_ready);
        }
        if (refinement_count != 0U) {
            (void)fprintf(stderr,
                          "MINIC_INLINE_INTEGER_SPECIALIZATION_TRANSITIVE_V0 refined=%zu clones=%zu functions=%zu\n",
                          refinement_count,
                          specialization_count,
                          program->function_count);
        }
    }
    if (specialization_count != 0U) {
        (void)fprintf(stderr,
                      "MINIC_INLINE_INTEGER_SPECIALIZATION_V0 clones=%zu functions=%zu\n",
                      specialization_count, program->function_count);
    }
    minic_core_reentry_index_destroy(pure_guard_reentry_index);
    return true;
}



