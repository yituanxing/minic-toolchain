#include "compiler/inline_specialize.h"
#include "compiler/inline_specialize_internal.h"
#include "compiler/inline_specialize_common.h"
#include "compiler/c_lower/opt_pure_statement.h"
#include "frontend/const_eval.h"
#include "frontend/function_body.h"
#include "target/backend_source.h"
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
    const MinicTargetInvocation *target;
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
                                                 const MinicTargetInvocation *target,
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
    const MinicC0Program *program, const MinicTargetInvocation *target,
    const MinicInlineCallPlan *call_plan) {
    unsigned char *blocked;
    const MinicSourceBackend *backend;
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
    backend = minic_source_backend_for_target(&target->machine);
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
            callee_id = minic_inline_call_plan_get(call_plan, index);
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
            callee_id = minic_inline_call_plan_get(call_plan, index);
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
                    if (!minic_source_backend_clone_safe_opaque_asm(backend, asm_source)) {
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
    /* The former source-AST finalizer propagated this to every clone.
     * Do it at creation now: Core preparation is the sole later authority
     * for pruning and must be able to lower each candidate variant. */
    specialized->is_referenced = source.is_referenced;
    specialized->specialization_source = source_id;
    specialized->is_integer_specialization = true;
    for (i = 0U; i < source.parameter_count; ++i) {
        specialized->specialization_integer_known[i] = known[i];
        specialized->specialization_integer_bits[i] = bits[i];
    }
    return true;
}



static bool minic_inline_integer_formal_admits_entry_fact(
    const MinicC0Program *program, const MinicFunction *source,
    size_t parameter_index) {
    const MinicLocal *local;
    if (program == NULL || source == NULL ||
        parameter_index >= source->parameter_count ||
        source->local_begin > SIZE_MAX - parameter_index)
        return false;
    local = minic_c0_program_local(
        program, source->local_begin + parameter_index);
    /* A top-level volatile parameter must be read as a volatile C object.
     * A literal actual argument does not turn its later reads into GNU
     * immediate operands or enable compile-time folding. */
    return local != NULL && !minic_type_is_volatile(local->type);
}

static bool minic_inline_specialization_argument_value(
    const MinicC0Program *program,
    const MinicTargetInvocation *target,
    const MinicFunction *caller,
    const bool *stable_parameter_locals,
    size_t stable_local_count,
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
                stable_parameter_locals,
                stable_local_count,
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
        /* A specialization fact describes the entry value. A reassigned or
         * escaped parameter is a different runtime value at this CALL, even
         * if all clones share the same canonical source FunctionBody. */
        if (stable_parameter_locals == NULL ||
            local_id >= stable_local_count ||
            !stable_parameter_locals[local_id] ||
            local_id < source->local_begin) {
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
    const MinicTargetInvocation *target, MinicInlineCallPlan *call_plan) {
    size_t expression_index;
    size_t original_function_count;
    size_t specialization_count = 0U;
    size_t static_specialization_count = 0U;
    unsigned char *static_blocked_sources = NULL;
    bool static_sources_checked = false;
    MinicCoreReentryIndex *pure_guard_reentry_index = NULL;
    bool pure_guard_index_attempted = false;
    MinicInlineInitialOwners initial_owners = {0};

    if (program == NULL || target == NULL || call_plan == NULL ||
        call_plan->program != program) {
        return false;
    }
    original_function_count = program->function_count;
    if (!minic_inline_initial_owners_build(program, original_function_count,
                                           &initial_owners)) {
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
        MinicInlineSpecializationKey key;
        size_t i;
        bool has_integer_constant = false;
        bool unresolved_constant_query = false;

        if (expression->kind != MINIC_EXPRESSION_CALL) {
            continue;
        }
        source_id = minic_inline_call_plan_get(call_plan, expression_index);
        if (source_id >= original_function_count) {
            continue;
        }
        source = program->functions[source_id];
        {
            MinicFunctionId owner_id = minic_inline_initial_owners_get(
                &initial_owners, expression_index);
            if (owner_id < original_function_count)
                caller = &program->functions[owner_id];
        }
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
                    program, target, call_plan);
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
        minic_inline_spec_key_clear(&key);
        for (i = 0U; i < source.parameter_count; ++i) {
            MinicConstValue argument_value;
            MinicConstValue converted_value;
            MinicExpressionId argument_id = minic_c0_call_argument(expression, i);
            const MinicExpression *argument_expression = minic_c0_program_expression(
                program, argument_id);
            bool argument_known;
            if (!minic_type_is_integer(source.parameter_types[i]) ||
                !minic_inline_integer_formal_admits_entry_fact(
                    program, &source, i)) {
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
                key.integer_known[i] = true;
                key.integer_bits[i] = converted_value.bits;
                has_integer_constant = true;
            } else if (minic_inline_integer_unresolved_constant_query(
                           program, argument_id, 0U)) {
                /* This speculative 0 must be revalidated in Core at the
                 * real callsite; never replace the shared source CALL. */
                key.integer_known[i] = true;
                key.integer_bits[i] = 0U;
                unresolved_constant_query = true;
            }
        }
        if (!has_integer_constant && !unresolved_constant_query) {
            continue;
        }
        specialized_id = minic_inline_spec_find_variant(
            program, &source, caller, source_id, &key,
            MINIC_INLINE_SPEC_FACT_INTEGER, original_function_count);
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
                minic_inline_initial_owners_destroy(&initial_owners);
                return false;
            }
            if (!minic_add_inline_integer_specialization(program, source_id, caller,
                                                         key.integer_known, key.integer_bits,
                                                         specialization_count,
                                                         &specialized_id)) {
                (void)fprintf(stderr,
                              "MINIC_INLINE_INTEGER_SPECIALIZATION_ADD_FAILED count=%zu functions=%zu source=%zu\n",
                              specialization_count,
                              program->function_count,
                              (size_t)source_id);
                minic_core_reentry_index_destroy(pure_guard_reentry_index);
                free(static_blocked_sources);
                minic_inline_initial_owners_destroy(&initial_owners);
                return false;
            }
            specialization_count += 1U;
            if (!source.is_inline) {
                static_specialization_count += 1U;
            }
        }
        if (!unresolved_constant_query && !minic_inline_call_plan_set(
                call_plan, expression_index, specialized_id)) {
            minic_core_reentry_index_destroy(pure_guard_reentry_index);
            free(static_blocked_sources);
            minic_inline_initial_owners_destroy(&initial_owners);
            return false;
        }
    }
    free(static_blocked_sources);
    minic_inline_initial_owners_destroy(&initial_owners);

    {
        size_t caller_index;
        size_t refinement_count = 0U;
        MinicInlineCallWorklist calls = {0};
        bool *stable_parameter_locals = NULL;
        size_t stable_local_count = program->local_count;
        size_t integer_call_candidates = 0U;
        size_t integer_owner_skipped = 0U;
        /* One source-wide, conservative stability screen for ALL transitive
         * parameter facts. If unavailable, speculative transitive facts are
         * disabled; a missing optional proof must never fabricate a clone. */
        if (stable_local_count <= SIZE_MAX / sizeof(*stable_parameter_locals)) {
            stable_parameter_locals = calloc(
                stable_local_count == 0U ? 1U : stable_local_count,
                sizeof(*stable_parameter_locals));
            if (stable_parameter_locals != NULL &&
                !minic_c0_program_stable_parameter_locals(
                    program, stable_parameter_locals, stable_local_count)) {
                free(stable_parameter_locals);
                stable_parameter_locals = NULL;
            }
        }
        /* Stable CALL IDs are grouped by their canonical FunctionBody
         * source; unowned/shared and late-appended expressions still visit
         * each caller. Allocation failure reverts to the complete scan. */
        if (!minic_inline_call_worklist_build(
                program, original_function_count, &calls)) {
            free(stable_parameter_locals);
            minic_core_reentry_index_destroy(pure_guard_reentry_index);
            return false;
        }

        for (caller_index = original_function_count;
             caller_index < program->function_count;
             ++caller_index) {
            const MinicFunction caller = program->functions[caller_index];
            MinicInlineCallCursor cursor;
            MinicExpressionId nested_expression_index;

            if (!caller.is_integer_specialization ||
                caller.specialization_source >= original_function_count) {
                continue;
            }
            minic_inline_call_cursor_begin(
                &calls, caller.specialization_source, &cursor);
            while (minic_inline_call_cursor_next(
                    &cursor, program, &nested_expression_index)) {
                const MinicExpression *nested = &program->expressions[nested_expression_index];
                MinicFunctionId current_callee_id;

                integer_call_candidates += 1U;
                MinicFunctionId nested_source_id;
                MinicFunctionId refined_id = MINIC_FUNCTION_INVALID;
                MinicFunction current_callee;
                MinicFunction nested_source;
                MinicInlineSpecializationKey nested_key;
                bool used_caller_fact = false;
                bool adds_fact = false;
                bool needs_section_clone;
                size_t parameter_index;

                if (nested->kind != MINIC_EXPRESSION_CALL) {
                    continue;
                }
                current_callee_id = minic_inline_call_plan_get(
                    call_plan, nested_expression_index);
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
                    !minic_inline_spec_section_matches(
                        &current_callee, &nested_source, &caller);
                minic_inline_spec_key_clear(&nested_key);
                for (parameter_index = 0U;
                     parameter_index < nested_source.parameter_count;
                     ++parameter_index) {
                    MinicConstValue argument_value;
                    bool argument_used_caller_fact = false;

                    if (!minic_type_is_integer(nested_source.parameter_types[parameter_index]) ||
                        !minic_inline_integer_formal_admits_entry_fact(
                            program, &nested_source, parameter_index) ||
                        !minic_inline_specialization_argument_value(
                            program,
                            target,
                            &caller,
                            stable_parameter_locals,
                            stable_local_count,
                            minic_c0_call_argument(nested, parameter_index),
                            nested_source.parameter_types[parameter_index],
                            &argument_value,
                            &argument_used_caller_fact)) {
                        continue;
                    }
                    nested_key.integer_known[parameter_index] = true;
                    nested_key.integer_bits[parameter_index] = argument_value.bits;
                    used_caller_fact = used_caller_fact || argument_used_caller_fact;
                    if (!current_callee.is_integer_specialization ||
                        !current_callee.specialization_integer_known[parameter_index] ||
                        current_callee.specialization_integer_bits[parameter_index] !=
                            argument_value.bits) {
                        adds_fact = true;
                    }
                }
                /* The direct plan can already select a clone for a different
                 * section. Recreate the same known-value facts in this
                 * caller's section even without a new transitive argument
                 * fact; otherwise Core may fall back to a generic helper. */
                if (needs_section_clone) {
                    for (parameter_index = 0U;
                         parameter_index < nested_source.parameter_count;
                         ++parameter_index) {
                        if (!nested_key.integer_known[parameter_index] &&
                            current_callee.specialization_integer_known[parameter_index]) {
                            nested_key.integer_known[parameter_index] = true;
                            nested_key.integer_bits[parameter_index] =
                                current_callee.specialization_integer_bits[parameter_index];
                        }
                    }
                }
                if ((!used_caller_fact || !adds_fact) && !needs_section_clone) {
                    continue;
                }
                refined_id = minic_inline_spec_find_variant(
                    program, &nested_source, &caller, nested_source_id,
                    &nested_key, MINIC_INLINE_SPEC_FACT_INTEGER,
                    original_function_count);
                if (refined_id == MINIC_FUNCTION_INVALID) {
                    if (specialization_count >= MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT) {
                        (void)fprintf(stderr,
                                      "MINIC_INLINE_INTEGER_SPECIALIZATION_TRANSITIVE_LIMIT_HIT count=%zu limit=%u functions=%zu source=%zu\n",
                                      specialization_count,
                                      (unsigned int)MINIC_INLINE_INTEGER_SPECIALIZATION_LIMIT,
                                      program->function_count,
                                      (size_t)nested_source_id);
                        minic_inline_call_worklist_destroy(&calls);
                        free(stable_parameter_locals);
                        minic_core_reentry_index_destroy(pure_guard_reentry_index);
                        return false;
                    }
                    if (!minic_add_inline_integer_specialization(
                            program,
                            nested_source_id,
                            &caller,
                            nested_key.integer_known,
                            nested_key.integer_bits,
                            specialization_count,
                            &refined_id)) {
                        (void)fprintf(stderr,
                                      "MINIC_INLINE_INTEGER_SPECIALIZATION_TRANSITIVE_ADD_FAILED count=%zu functions=%zu source=%zu\n",
                                      specialization_count,
                                      program->function_count,
                                      (size_t)nested_source_id);
                        minic_inline_call_worklist_destroy(&calls);
                        free(stable_parameter_locals);
                        minic_core_reentry_index_destroy(pure_guard_reentry_index);
                        return false;
                    }
                    specialization_count += 1U;
                    refinement_count += 1U;
                }
            }
            integer_owner_skipped += cursor.skipped_other_owner;
        }
        if (getenv("MINIC_INTEGER_OWNER_PERF") != NULL) {
            (void)fprintf(stderr,
                          "MINIC_INTEGER_OWNER_FILTER ready=%u "
                          "candidates=%zu skipped_other_owner=%zu "
                          "fallback=%u\n",
                          (unsigned int)calls.owner_ready,
                          integer_call_candidates,
                          integer_owner_skipped,
                          (unsigned int)!calls.owner_ready);
        }
        minic_inline_call_worklist_destroy(&calls);
        free(stable_parameter_locals);
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



