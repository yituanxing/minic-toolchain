#include "core/core_lower_internal.h"
#include "core/reentry_index.h"
#include "core/local_facts.h"
#include "core/cfg_constant_calls.h"
#include "frontend/const_eval.h"
#include "frontend/expression_semantics.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One owner for source-level if CFG, reentry safety and local-fact joins.
 * CFG terminators and structured statements remain target-independent. */

/* M187_CONSTANT_IF_EXTERNAL_REENTRY: mark every statement owned by a
   discarded block graph so internal gotos disappear with that graph rather
   than falsely keeping an otherwise constant-dead branch alive. */
static bool core_mark_discarded_if_membership(
    const MinicCoreLowerContext *context,
    const MinicBlock *block,
    bool *visited_blocks,
    bool *statement_membership) {
    const MinicC0Program *program;
    size_t index;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        block == NULL || visited_blocks == NULL || statement_membership == NULL) {
        return false;
    }
    program = context->body->program;
    for (index = 0U; index < block->statement_count; ++index) {
        MinicStatementId statement_id = block->statements[index];
        const MinicStatement *statement;

        if (statement_id >= program->statement_count) {
            return false;
        }
        statement_membership[statement_id] = true;
        statement = minic_c0_program_statement(program, statement_id);
        if (statement == NULL) {
            return false;
        }
        if (statement->then_block != MINIC_BLOCK_INVALID) {
            const MinicBlock *child;
            if (statement->then_block >= program->block_count) {
                return false;
            }
            if (!visited_blocks[statement->then_block]) {
                visited_blocks[statement->then_block] = true;
                child = minic_c0_program_block(program, statement->then_block);
                if (child == NULL ||
                    !core_mark_discarded_if_membership(
                        context, child, visited_blocks, statement_membership)) {
                    return false;
                }
            }
        }
        if (statement->else_block != MINIC_BLOCK_INVALID) {
            const MinicBlock *child;
            if (statement->else_block >= program->block_count) {
                return false;
            }
            if (!visited_blocks[statement->else_block]) {
                visited_blocks[statement->else_block] = true;
                child = minic_c0_program_block(program, statement->else_block);
                if (child == NULL ||
                    !core_mark_discarded_if_membership(
                        context, child, visited_blocks, statement_membership)) {
                    return false;
                }
            }
        }
    }
    return true;
}

static bool core_block_contains_reentry_label(const MinicCoreLowerContext *context,
                                              const MinicBlock *block) {
    const MinicC0Program *program;
    bool *visited_blocks;
    bool *statement_membership;
    bool unsafe;
    size_t index;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        block == NULL) {
        return true;
    }
    program = context->body->program;
    if (program->statement_count == 0U ||
        program->block_count > SIZE_MAX / sizeof(*visited_blocks) ||
        program->statement_count > SIZE_MAX / sizeof(*statement_membership)) {
        return true;
    }
    visited_blocks = program->block_count == 0U
                         ? NULL
                         : (bool *)calloc(program->block_count, sizeof(*visited_blocks));
    statement_membership =
        (bool *)calloc(program->statement_count, sizeof(*statement_membership));
    if ((program->block_count != 0U && visited_blocks == NULL) ||
        statement_membership == NULL) {
        free(visited_blocks);
        free(statement_membership);
        return true;
    }
    unsafe = !core_mark_discarded_if_membership(
        context, block, visited_blocks, statement_membership);

    if (minic_core_reentry_index_ready(context->reentry_index, program)) {
        minic_core_reentry_index_check_discarded(
            context->reentry_index, program, statement_membership, &unsafe);
    } else {
        /* case/default labels are entered by switch dispatch, independently of
           lexical fallthrough, so never prune a subtree that owns one. */
        for (index = 0U; index < program->statement_count && !unsafe; ++index) {
            const MinicStatement *statement;
            if (!statement_membership[index]) {
                continue;
            }
            statement = minic_c0_program_statement(program, index);
            if (statement == NULL || statement->kind == MINIC_STATEMENT_CASE ||
                statement->kind == MINIC_STATEMENT_DEFAULT) {
                unsafe = true;
            }
        }

        /* Only jumps originating outside the discarded membership are external
           re-entry. Internal goto/asm-goto edges disappear with the subtree. */
        for (index = 0U; index < program->statement_count && !unsafe; ++index) {
            const MinicStatement *source;
            if (statement_membership[index]) {
                continue;
            }
            source = minic_c0_program_statement(program, index);
            if (source == NULL) {
                unsafe = true;
                break;
            }
            if (source->kind == MINIC_STATEMENT_GOTO &&
                source->target_statement < program->statement_count &&
                statement_membership[source->target_statement]) {
                unsafe = true;
                break;
            }
            if (source->kind == MINIC_STATEMENT_INLINE_ASM &&
                source->inline_asm_id < program->inline_asm_count) {
                const MinicInlineAsm *inline_asm = &program->inline_asms[source->inline_asm_id];
                size_t label_index;
                if (!inline_asm->is_goto) {
                    continue;
                }
                for (label_index = 0U; label_index < inline_asm->label_count; ++label_index) {
                    MinicStatementId target = inline_asm->labels[label_index].target_statement;
                    if (target < program->statement_count && statement_membership[target]) {
                        unsafe = true;
                        break;
                    }
                }
            }
        }

        /* An address-taken label may be reached through computed goto from code
           that is not represented as a direct target edge here. Keep it alive. */
        for (index = 0U; index < program->expression_count && !unsafe; ++index) {
            const MinicExpression *expression = minic_c0_program_expression(program, index);
            if (expression == NULL) {
                unsafe = true;
                break;
            }
            if (expression->kind == MINIC_EXPRESSION_LABEL_ADDRESS &&
                expression->value.label_statement_id < program->statement_count &&
                statement_membership[expression->value.label_statement_id]) {
                unsafe = true;
            }
        }

    }

    free(visited_blocks);
    free(statement_membership);
    return unsafe;
}


/* M192_IF_LOCAL_FACT_MEET */
static void core_local_constants_prepare_if_branch(
    MinicCoreLowerContext *context,
    const MinicCoreLocalIntegerConstant *incoming,
    MinicExpressionId condition_expression) {
    size_t index;

    if (context == NULL || context->source_function == NULL ||
        context->local_integer_constants == NULL || incoming == NULL) {
        return;
    }
    for (index = 0U; index < context->source_function->local_count; ++index) {
        MinicLocalId local_id = context->source_function->local_begin + index;
        const MinicLocal *local = minic_c0_program_local(
            context->body->program, local_id);
        const MinicExpression *condition = minic_c0_program_expression(
            context->body->program, condition_expression);
        context->local_integer_constants[index] = incoming[index];
        /* Local IDs cover the entire function, including block-scoped objects
         * declared after this condition.  An unknown side effect in the
         * condition cannot possibly escape an object whose lifetime has not
         * begun.  Poisoning that future object makes subsequent constant
         * initializers unusable as GNU asm `i` operands.  Zero/unknown spans
         * are kept conservative for compiler-generated temporaries. */
        if (local != NULL && condition != NULL &&
            local->name_span.begin.offset != 0U &&
            condition->span.end.offset != 0U &&
            local->name_span.begin.offset >= condition->span.end.offset) {
            continue;
        }
        if (incoming[index].escaped ||
            core_expression_may_modify_local(
                context, condition_expression, local_id, 0U)) {
            context->local_integer_constants[index].known = false;
            /* Structural branch lowering happens before condition lowering.
               Treat a condition that may expose/modify this local as escaped
               for branch fact purposes; this is conservative and prevents a
               later branch assignment from manufacturing an unsafe fact. */
            context->local_integer_constants[index].escaped = true;
        }
    }
}

static void core_local_constants_meet_if_paths(
    MinicCoreLowerContext *context,
    const MinicCoreLocalIntegerConstant *then_facts,
    bool then_continues,
    const MinicCoreLocalIntegerConstant *else_facts,
    bool else_continues) {
    size_t index;

    if (context == NULL || context->source_function == NULL ||
        context->local_integer_constants == NULL) {
        return;
    }
    if (then_continues && !else_continues && then_facts != NULL) {
        minic_core_local_facts_restore(context, then_facts);
        return;
    }
    if (!then_continues && else_continues && else_facts != NULL) {
        minic_core_local_facts_restore(context, else_facts);
        return;
    }
    if (!then_continues && !else_continues) {
        minic_core_local_facts_clear_known(context);
        return;
    }
    if (then_facts == NULL || else_facts == NULL) {
        minic_core_local_facts_clear_known(context);
        return;
    }

    for (index = 0U; index < context->source_function->local_count; ++index) {
        const MinicCoreLocalIntegerConstant *left = &then_facts[index];
        const MinicCoreLocalIntegerConstant *right = &else_facts[index];
        MinicCoreLocalIntegerConstant merged = *left;
        bool same_value =
            left->known && right->known &&
            !left->escaped && !right->escaped &&
            minic_type_equal(left->value.type, right->value.type) &&
            left->value.bits == right->value.bits;

        merged.escaped = left->escaped || right->escaped;
        merged.specialization_modified =
            left->specialization_modified || right->specialization_modified;
        merged.specialization_known =
            left->specialization_known && right->specialization_known &&
            minic_type_equal(left->specialization_value.type,
                             right->specialization_value.type) &&
            left->specialization_value.bits == right->specialization_value.bits;
        merged.known = same_value && !merged.escaped;
        context->local_integer_constants[index] = merged;
    }
}

/* INLINE_ASM_IF_FACT_BARRIER: a side-effecting GNU asm block must
   not be made unconditional by a guessed straight-line local fact. */
static bool core_block_contains_inline_asm_impl(
    const MinicCoreLowerContext *context,
    const MinicBlock *block,
    bool *visited) {
    size_t index;
    const MinicC0Program *program;
    if (context == NULL || context->body == NULL ||
        (program = context->body->program) == NULL ||
        block == NULL || visited == NULL) {
        return true;
    }
    for (index = 0U; index < block->statement_count; ++index) {
        const MinicStatement *statement = minic_c0_program_statement(
            program, block->statements[index]);
        const MinicBlock *nested;
        if (statement == NULL) {
            return true;
        }
        /* The parser normalizes do { asm } while (0) to a synthetic
           WHILE. Its inline effects can be hidden behind an internal
           one-pass form, so treat any dynamic structured loop as an
           optimization barrier, not as an unsafe constant-fold witness. */
        if (statement->kind == MINIC_STATEMENT_INLINE_ASM ||
            statement->kind == MINIC_STATEMENT_WHILE) {
            return true;
        }
        if (statement->then_block != MINIC_BLOCK_INVALID) {
            if (statement->then_block >= program->block_count) {
                return true;
            }
            if (!visited[statement->then_block]) {
                visited[statement->then_block] = true;
                nested = minic_c0_program_block(program, statement->then_block);
                if (core_block_contains_inline_asm_impl(context, nested, visited)) {
                    return true;
                }
            }
        }
        if (statement->else_block != MINIC_BLOCK_INVALID) {
            if (statement->else_block >= program->block_count) {
                return true;
            }
            if (!visited[statement->else_block]) {
                visited[statement->else_block] = true;
                nested = minic_c0_program_block(program, statement->else_block);
                if (core_block_contains_inline_asm_impl(context, nested, visited)) {
                    return true;
                }
            }
        }
    }
    return false;
}

static bool core_block_contains_inline_asm(
    const MinicCoreLowerContext *context, const MinicBlock *block) {
    bool *visited;
    bool contains;
    const MinicC0Program *program;
    if (context == NULL || context->body == NULL ||
        (program = context->body->program) == NULL ||
        block == NULL || program->block_count == 0U) {
        return true;
    }
    visited = (bool *)calloc(program->block_count, sizeof(*visited));
    if (visited == NULL) {
        return true;
    }
    contains = core_block_contains_inline_asm_impl(context, block, visited);
    free(visited);
    return contains;
}


/* RUNTIME_IF_FALLTHROUGH_FACTS_V1: preserve unescaped locals through a
   precisely constrained read-only-local guard or direct call whose arguments
   cannot provide an address of a local being propagated. */
/* A direct function call can mutate global memory, but cannot write a
   currently unescaped automatic local unless its address is supplied.
   Accept only direct function calls whose arguments contain no address-of,
   assignments, increments, volatile reads, or arbitrary expressions.
   The Core fact table independently tracks local address escapes. */
static bool core_runtime_if_guard_preserves_locals_depth(
    const MinicCoreLowerContext *context, MinicExpressionId id,
    unsigned int depth) {
    const MinicC0Program *program;
    const MinicExpression *expression;
    const MinicLocal *local;
    size_t i;

    if (context == NULL || context->body == NULL ||
        context->body->program == NULL || depth > 24U) {
        return false;
    }
    program = context->body->program;
    expression = minic_c0_program_expression(program, id);
    if (expression == NULL) {
        return false;
    }
    if (expression->kind == MINIC_EXPRESSION_UNARY &&
        expression->value.unary.operator_kind == MINIC_UNARY_LOGICAL_NOT) {
        return core_runtime_if_guard_preserves_locals_depth(
            context, expression->value.unary.operand, depth + 1U);
    }
    if (expression->kind == MINIC_EXPRESSION_CAST ||
        expression->kind == MINIC_EXPRESSION_CONVERSION ||
        expression->kind == MINIC_EXPRESSION_BITCAST ||
        expression->kind == MINIC_EXPRESSION_LVALUE_READ) {
        return core_runtime_if_guard_preserves_locals_depth(
            context, expression->value.unary.operand, depth + 1U);
    }
    if (expression->kind == MINIC_EXPRESSION_LOCAL) {
        local = minic_c0_program_local(program, expression->value.local_id);
        return local != NULL && !minic_type_is_volatile(local->type);
    }
    if (expression->kind == MINIC_EXPRESSION_INTEGER) {
        return true;
    }
    if (expression->kind == MINIC_EXPRESSION_CALL) {
        /* Direct calls store the function_id, not a source callee expr.
           Indirect calls have function_id == MINIC_FUNCTION_INVALID. */
        if (expression->value.call.function_id == MINIC_FUNCTION_INVALID ||
            minic_c0_program_function(
                program, expression->value.call.function_id) == NULL) {
            return false;
        }
        for (i = 0U; i < expression->value.call.argument_count; ++i) {
            if (!core_runtime_if_guard_preserves_locals_depth(
                    context, minic_c0_call_argument(expression, i), depth + 1U)) {
                return false;
            }
        }
        return true;
    }
    return false;
}

static bool core_runtime_if_pure_local_guard(
    const MinicCoreLowerContext *context, MinicExpressionId id) {
    return core_runtime_if_guard_preserves_locals_depth(context, id, 0U);
}

static MinicCoreLocalIntegerConstant *core_runtime_if_copy_facts(
    const MinicCoreLowerContext *context) {
    MinicCoreLocalIntegerConstant *copy;
    size_t count;

    if (context == NULL || context->source_function == NULL ||
        context->local_integer_constants == NULL) {
        return NULL;
    }
    count = context->source_function->local_count;
    if (count == 0U || count > SIZE_MAX / sizeof(*copy)) {
        return NULL;
    }
    copy = (MinicCoreLocalIntegerConstant *)malloc(count * sizeof(*copy));
    if (copy != NULL) {
        (void)memcpy(copy, context->local_integer_constants,
                     count * sizeof(*copy));
    }
    return copy;
}

static void core_runtime_if_merge_false_fallthrough(
    MinicCoreLowerContext *context,
    const MinicCoreLocalIntegerConstant *incoming,
    const MinicCoreLocalIntegerConstant *then_exit,
    bool then_terminated) {
    size_t i;

    if (context == NULL || context->source_function == NULL ||
        context->local_integer_constants == NULL || incoming == NULL) {
        return;
    }
    for (i = 0U; i < context->source_function->local_count; ++i) {
        const MinicCoreLocalIntegerConstant *before = &incoming[i];
        const MinicCoreLocalIntegerConstant *after =
            then_exit == NULL ? NULL : &then_exit[i];
        /* The false edge always continues with incoming local values.
           If the true edge also continues, it must prove the SAME value. */
        if (!before->known || before->escaped ||
            (!then_terminated &&
             (after == NULL || !after->known || after->escaped ||
              !minic_type_equal(before->value.type, after->value.type) ||
              before->value.bits != after->value.bits))) {
            continue;
        }
        context->local_integer_constants[i] = *before;
        if (!then_terminated && after != NULL) {
            context->local_integer_constants[i].specialization_modified =
                before->specialization_modified ||
                after->specialization_modified;
            context->local_integer_constants[i].escaped =
                before->escaped || after->escaped;
            if (context->local_integer_constants[i].specialization_modified) {
                context->local_integer_constants[i].specialization_known = false;
            }
        }
    }
}
MinicCoreLowerStatus
minic_core_lower_if(MinicCoreLowerContext *context, const MinicStatement *statement, bool *terminated) {
    const MinicBlock *else_source;
    const MinicBlock *then_source;
    const MinicExpression *condition_expression;
    MinicCoreBlockId condition_block;
    MinicCoreBlockId else_block;
    MinicCoreBlockId false_target;
    MinicCoreBlockId merge_block;
    MinicCoreBlockId then_block;
    MinicCoreBlockId then_continuation_block;
    MinicCoreBlockId else_continuation_block;
    MinicCoreBlockId continuation_block;
    MinicCoreLowerStatus status;
    MinicCoreLocalIntegerConstant *fallthrough_facts = NULL;
    MinicCoreLocalIntegerConstant *then_exit_facts = NULL;
    MinicCoreLocalIntegerConstant *incoming_facts;
    MinicCoreLocalIntegerConstant *branch_entry_facts;
    MinicCoreLocalIntegerConstant *then_facts;
    MinicCoreLocalIntegerConstant *else_facts;
    bool else_terminated;
    bool needs_merge;
    bool then_terminated;
    bool preserve_guard_facts;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || statement == NULL || terminated == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    condition_expression =
        minic_c0_program_expression(context->body->program, statement->expression);
    then_source = minic_c0_program_block(context->body->program, statement->then_block);
    if (condition_expression == NULL || then_source == NULL ||
        (!minic_type_is_integer(condition_expression->type) &&
         !minic_type_is_pointer(condition_expression->type))) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    else_source = NULL;
    if (statement->else_block != MINIC_BLOCK_INVALID) {
        else_source = minic_c0_program_block(context->body->program, statement->else_block);
        if (else_source == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
    }

    if (statement->cleanup_context == statement->cleanup_stop_context &&
        minic_type_is_integer(condition_expression->type)) {
        bool condition_is_zero;
        /* Independent zero-only bool reachability: keep the original
           Runtime-profile integer and pointer if-seams byte-identical. */
        if (core_zero_bool_cfg_condition(
                context, statement->expression, 0U, &condition_is_zero)) {
            const MinicBlock *selected_source =
                condition_is_zero ? else_source : then_source;
            const MinicBlock *discarded_source =
                condition_is_zero ? then_source : else_source;
            if (discarded_source == NULL ||
                !core_block_contains_reentry_label(context, discarded_source)) {
                if (selected_source == NULL) {
                    *terminated = false;
                    return MINIC_CORE_LOWER_OK;
                }
                return minic_core_lower_internal_lower_block(context, selected_source, terminated);
            }
        }

    }

    /* M182_TERMINATING_GUARD_FACTS: a pure terminating taken arm does not
       contribute state to the following statement. */
    preserve_guard_facts = false;
    if (else_source == NULL && then_source->statement_count == 1U &&
        minic_core_cfg_pure_call_argument(context, statement->expression, 0U)) {
        const MinicStatement *guard_statement = minic_c0_program_statement(
            context->body->program, then_source->statements[0]);
        preserve_guard_facts =
            guard_statement != NULL &&
            (guard_statement->kind == MINIC_STATEMENT_GOTO ||
             guard_statement->kind == MINIC_STATEMENT_BREAK);
    }

    /* MINIC_DMA_IF_TRACE_V0 */
    if (getenv("MINIC_DMA_IF_TRACE") != NULL &&
        context->source_function != NULL &&
        context->source_function->name != NULL &&
        strcmp(context->source_function->name, "dma_direct_alloc") == 0) {
        MinicConstValue traced;
        MinicLocalId traced_local = MINIC_LOCAL_INVALID;
        size_t fact_idx = 0U;
        bool local_read = minic_core_lower_direct_local_read(
            context, statement->expression, &traced_local);
        bool fact_local = local_read &&
            minic_core_local_fact_index(context, traced_local, &fact_idx);
        bool known = core_const_eval_integer_with_locals(
            context, statement->expression, &traced);
        (void)fprintf(stderr,
            "DMA_IF_TRACE line=%zu expr_kind=%d read_local=%d local=%zu known=%d bits=%llu fact_known=%d escaped=%d stable=%d\\n",
            statement->span.begin.line,
            (int)condition_expression->kind, local_read ? 1 : 0,
            (size_t)traced_local, known ? 1 : 0,
            known ? (unsigned long long)traced.bits : 0ULL,
            fact_local && context->local_integer_constants[fact_idx].known ? 1 : 0,
            fact_local && context->local_integer_constants[fact_idx].escaped ? 1 : 0,
            fact_local && context->local_integer_constants[fact_idx].program_zero_stable ? 1 : 0);
    }
    /* MINIC_SHMEM_IF_TRACE_V0 */
    if (getenv("MINIC_SHMEM_IF_TRACE") != NULL &&
        context->source_function != NULL &&
        context->source_function->name != NULL &&
        strcmp(context->source_function->name, "shmem_alloc_and_acct_folio") == 0) {
        MinicConstValue traced;
        MinicLocalId traced_local = MINIC_LOCAL_INVALID;
        size_t fact_idx = 0U;
        bool local_read = minic_core_lower_direct_local_read(
            context, statement->expression, &traced_local);
        bool fact_local = local_read &&
            minic_core_local_fact_index(context, traced_local, &fact_idx);
        bool known = core_const_eval_integer_with_locals(
            context, statement->expression, &traced);
        (void)fprintf(stderr,
            "SHMEM_IF_TRACE line=%zu expr_kind=%d read_local=%d local=%zu known=%d bits=%llu fact_known=%d escaped=%d stable=%d\n",
            statement->span.begin.line,
            (int)condition_expression->kind, local_read ? 1 : 0,
            (size_t)traced_local, known ? 1 : 0,
            known ? (unsigned long long)traced.bits : 0ULL,
            fact_local && context->local_integer_constants[fact_idx].known ? 1 : 0,
            fact_local && context->local_integer_constants[fact_idx].escaped ? 1 : 0,
            fact_local && context->local_integer_constants[fact_idx].program_zero_stable ? 1 : 0);
    }
    /* BusyBox and ordinary GNU C intentionally leave impossible references in
       branches guarded by target constants such as sizeof comparisons and
       ENABLE_* macros.  GCC removes those branches before emission.  Preserve
       the same language-level reachability fact in Core: when the existing
       target-aware integer const-evaluator proves an if condition, lower only
       the selected source block.  A discarded subtree that owns any C/case/
       default label stays on the general CFG path because an enclosing goto or
       switch may legally enter it. */
    if (statement->cleanup_context == statement->cleanup_stop_context) {
        bool condition_is_zero = false;
        bool condition_known = false;

        if (minic_type_is_pointer(condition_expression->type) &&
            minic_core_lower_known_null_pointer(context, statement->expression)) {
            condition_known = true;
            condition_is_zero = true;
        } else if (minic_type_is_integer(condition_expression->type)) {
            MinicConstValue condition_value;
            bool integer_condition_is_zero;

            if ((minic_const_eval_integer(context->body->program,
                                          context->target,
                                          statement->expression, &condition_value) ||
                 (!core_block_contains_inline_asm(context, then_source) &&
                  (else_source == NULL ||
                   !core_block_contains_inline_asm(context, else_source)) &&
                  core_const_eval_integer_with_locals(
                      context, statement->expression, &condition_value))) &&
                minic_const_value_is_zero(context->body->program,
                                          context->target,
                                          &condition_value,
                                          &integer_condition_is_zero)) {
                condition_known = true;
                condition_is_zero = integer_condition_is_zero;
            }
        }
        if (context->source_function != NULL &&
            context->source_function->name != NULL &&
            strcmp(context->source_function->name, "genl_op_from_split") == 0) {
            (void)fprintf(stderr,
                          "GENL_IF_DECISION expression=%zu kind=%d known=%d zero=%d block=%u\n",
                          (size_t)statement->expression,
                          (int)condition_expression->kind,
                          condition_known ? 1 : 0,
                          condition_is_zero ? 1 : 0,
                          (unsigned int)context->block_id);
        }
        if (condition_known) {
            const MinicBlock *discarded_source;
            const MinicBlock *selected_source;

            selected_source = condition_is_zero ? else_source : then_source;
            discarded_source = condition_is_zero ? then_source : else_source;
            if (discarded_source == NULL ||
                !core_block_contains_reentry_label(context, discarded_source)) {
                if (selected_source == NULL) {
                    *terminated = false;
                    return MINIC_CORE_LOWER_OK;
                }
                return minic_core_lower_internal_lower_block(context, selected_source, terminated);
            }
        }
    }

    fallthrough_facts = NULL;
    then_exit_facts = NULL;
    if (else_source == NULL &&
        core_runtime_if_pure_local_guard(context, statement->expression) &&
        !core_block_contains_reentry_label(context, then_source)) {
        fallthrough_facts = core_runtime_if_copy_facts(context);
    }
    if (getenv("MINIC_SHMEM_IF_TRACE") != NULL &&
        context->source_function != NULL &&
        strcmp(context->source_function->name, "shmem_alloc_and_acct_folio") == 0) {
        size_t dbg_idx;
        if (minic_core_local_fact_index(context, (MinicLocalId)10022U, &dbg_idx)) {
            (void)fprintf(stderr, "SHMEM_IF_FLOW line=%zu edge=entry huge_known=%d val=%llu snap=%d pure=%d reentry=%d\n",
               statement->span.begin.line,
               context->local_integer_constants[dbg_idx].known ? 1 : 0,
               (unsigned long long)context->local_integer_constants[dbg_idx].value.bits,
               fallthrough_facts != NULL ? 1 : 0,
               core_runtime_if_pure_local_guard(context, statement->expression) ? 1 : 0,
               core_block_contains_reentry_label(context, then_source) ? 1 : 0);
        }
    }
    incoming_facts = minic_core_local_facts_snapshot(context);
    if (context->source_function != NULL &&
        context->source_function->local_count != 0U && incoming_facts == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }
    branch_entry_facts = NULL;
    then_facts = NULL;
    else_facts = NULL;
    if (!preserve_guard_facts) {
        minic_core_local_facts_clear_known(context);
        core_local_constants_prepare_if_branch(
            context, incoming_facts, statement->expression);
    }
    branch_entry_facts = minic_core_local_facts_snapshot(context);
    if (context->source_function != NULL &&
        context->source_function->local_count != 0U && branch_entry_facts == NULL) {
        free(incoming_facts);
        return MINIC_CORE_LOWER_ERROR;
    }
    condition_block = context->block_id;
    if (!minic_core_function_add_block(context->function, &then_block)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    else_block = MINIC_CORE_BLOCK_INVALID;
    if (else_source != NULL && !minic_core_function_add_block(context->function, &else_block)) {
        return MINIC_CORE_LOWER_ERROR;
    }

    context->block_id = then_block;
    if (context->source_function && context->source_function->name &&
        strcmp(context->source_function->name, "genl_op_from_split") == 0)
        (void)fprintf(stderr, "GENL_IF_PATH stage=pre_then expr=%zu current=%u then=%u condition=%u\n",
            (size_t)statement->expression, (unsigned int)context->block_id,
            (unsigned int)then_block, (unsigned int)condition_block);
    status = minic_core_lower_internal_lower_block(context, then_source, &then_terminated);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr,
                      "CORE_FAST_TRACE stage=if reason=then-body function=%s "
                      "status=%d condition_kind=%d span=%zu:%zu\n",
                      context->source_function->name,
                      (int)status,
                      (int)condition_expression->kind,
                      statement->span.begin.line,
                      statement->span.begin.column);
        return status;
    }
    if (context->source_function && context->source_function->name &&
        strcmp(context->source_function->name, "genl_op_from_split") == 0)
        (void)fprintf(stderr, "GENL_IF_PATH stage=post_then expr=%zu status=%d terminated=%d continuation=%u\n",
            (size_t)statement->expression, (int)status,
            then_terminated ? 1 : 0, (unsigned int)context->block_id);
    then_continuation_block = context->block_id;
    if (fallthrough_facts != NULL) {
        then_exit_facts = core_runtime_if_copy_facts(context);
    }
    then_facts = minic_core_local_facts_snapshot(context);
    if (context->source_function != NULL &&
        context->source_function->local_count != 0U && then_facts == NULL) {
        free(branch_entry_facts);
        free(incoming_facts);
        return MINIC_CORE_LOWER_ERROR;
    }
    else_continuation_block = MINIC_CORE_BLOCK_INVALID;
    else_terminated = false;
    if (else_source != NULL) {
        minic_core_local_facts_restore(context, branch_entry_facts);
        context->block_id = else_block;
        status = minic_core_lower_internal_lower_block(context, else_source, &else_terminated);
        if (status != MINIC_CORE_LOWER_OK) {
            (void)fprintf(stderr,
                          "CORE_FAST_TRACE stage=if reason=else-body function=%s "
                          "status=%d condition_kind=%d span=%zu:%zu\n",
                          context->source_function->name,
                          (int)status,
                          (int)condition_expression->kind,
                          statement->span.begin.line,
                          statement->span.begin.column);
            return status;
        }
        else_continuation_block = context->block_id;
        else_facts = minic_core_local_facts_snapshot(context);
        if (context->source_function != NULL &&
            context->source_function->local_count != 0U && else_facts == NULL) {
            free(then_facts);
            free(branch_entry_facts);
            free(incoming_facts);
            return MINIC_CORE_LOWER_ERROR;
        }
    }

    needs_merge = !then_terminated || else_source == NULL || !else_terminated;
    merge_block = MINIC_CORE_BLOCK_INVALID;
    if (needs_merge) {
        if (!minic_core_function_add_block(context->function, &merge_block)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (!then_terminated) {
            status = minic_core_lower_internal_set_branch(context, then_continuation_block, statement->span, merge_block);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
        if (else_source != NULL && !else_terminated) {
            status = minic_core_lower_internal_set_branch(context, else_continuation_block, statement->span, merge_block);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
        false_target = else_source == NULL ? merge_block : else_block;
        context->block_id = merge_block;
    } else {
        false_target = else_block;
        context->block_id = condition_block;
    }

    continuation_block = context->block_id;
    context->block_id = condition_block;
    minic_core_local_facts_restore(context, incoming_facts);
    /* DOMINANCE_IF_GUARD_FACT_BARRIER: speculative body facts do not
       dominate the earlier condition_block. Rebuild the runtime predicate
       with only immutable source/specialization facts, not body updates. */
    minic_core_local_facts_clear_known(context);
    if (context->source_function && context->source_function->name &&
        strcmp(context->source_function->name, "genl_op_from_split") == 0)
        (void)fprintf(stderr, "GENL_IF_PATH stage=pre_guard expr=%zu condition=%u then=%u else=%u need_merge=%d\n",
            (size_t)statement->expression, (unsigned int)condition_block,
            (unsigned int)then_block, (unsigned int)false_target,
            needs_merge ? 1 : 0);
    status = minic_core_lower_internal_lower_condition_branch(
        context, statement->expression, statement->span, then_block, false_target);
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr,
                      "CORE_FAST_TRACE stage=if reason=condition function=%s "
                      "status=%d condition_kind=%d span=%zu:%zu\n",
                      context->source_function->name,
                      (int)status,
                      (int)condition_expression->kind,
                      statement->span.begin.line,
                      statement->span.begin.column);
        free(incoming_facts);
        return status;
    }
    context->block_id = continuation_block;
    if (!(else_source == NULL && then_terminated)) {
        core_local_constants_meet_if_paths(
            context,
            then_facts,
            !then_terminated,
            else_source == NULL ? branch_entry_facts : else_facts,
            else_source == NULL ? true : !else_terminated);
    }
    free(else_facts);
    free(then_facts);
    free(branch_entry_facts);
    if (else_source == NULL) {
        core_runtime_if_merge_false_fallthrough(
            context, fallthrough_facts, then_exit_facts, then_terminated);
    }
    free(then_exit_facts);
    free(fallthrough_facts);
    if (getenv("MINIC_SHMEM_IF_TRACE") != NULL &&
        context->source_function != NULL &&
        strcmp(context->source_function->name, "shmem_alloc_and_acct_folio") == 0) {
        size_t dbg_idx;
        if (minic_core_local_fact_index(context, (MinicLocalId)10022U, &dbg_idx)) {
            (void)fprintf(stderr, "SHMEM_IF_FLOW line=%zu edge=merge huge_known=%d val=%llu terminated=%d\n",
                statement->span.begin.line,
                context->local_integer_constants[dbg_idx].known ? 1 : 0,
                (unsigned long long)context->local_integer_constants[dbg_idx].value.bits,
                then_terminated ? 1 : 0);
        }
    }
    free(incoming_facts);
    if (context->source_function && context->source_function->name &&
        strcmp(context->source_function->name, "genl_op_from_split") == 0)
        (void)fprintf(stderr, "GENL_IF_PATH stage=post_guard expr=%zu status=%d block=%u\n",
            (size_t)statement->expression, (int)status,
            (unsigned int)context->block_id);
    *terminated = !needs_merge;
    return MINIC_CORE_LOWER_OK;
}

