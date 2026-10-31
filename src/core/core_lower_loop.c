#include "core/core_lower_internal.h"
#include "core/local_facts.h"
#include "core/source_loop_shapes.h"
#include "frontend/const_eval.h"
#include "frontend/expression_semantics.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Target-independent loop CFG: normalized while/for/do-while, backedges,
 * break/continue reachability and conservative local fact stabilization. */

/* M132_UNBOUNDED_FOR_TERMINATION: structured control-flow reachability is
   the owner of whether a synthetic loop exit can fall through. In particular,
   an omitted-condition GNU/C `for (;;)` has no natural edge to its exit block;
   only an explicit break may make that exit reachable. */
bool minic_core_lower_internal_block_has_predecessor(const MinicCoreFunction *function,
                                       MinicCoreBlockId target) {
    size_t block_index;

    if (function == NULL || target == MINIC_CORE_BLOCK_INVALID ||
        target >= function->block_count) {
        return false;
    }
    for (block_index = 0U; block_index < function->block_count; ++block_index) {
        const MinicCoreBlock *block = &function->blocks[block_index];
        if (!block->has_terminator) {
            continue;
        }
        if (block->terminator.kind == MINIC_CORE_TERMINATOR_BRANCH &&
            block->terminator.branch_target == target) {
            return true;
        }
        if (block->terminator.kind == MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH &&
            (block->terminator.conditional.when_true == target ||
             block->terminator.conditional.when_false == target)) {
            return true;
        }
    }
    return false;
}

/* M133_CONSTANT_TRUE_LOOP_REACHABILITY: a loop condition that Sema/const-eval
   proves to be a nonzero integer constant has no natural false edge. Keep this
   a target-neutral CFG fact: explicit break edges still make the synthetic exit
   reachable, while an otherwise unreachable exit terminates the enclosing path. */
static bool core_loop_condition_is_constant_true(const MinicCoreLowerContext *context,
                                                 MinicExpressionId expression_id) {
    MinicConstValue value;
    bool is_zero;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->target == NULL || expression_id == MINIC_EXPRESSION_INVALID ||
        !minic_const_eval_integer(
            context->body->program, context->target, expression_id, &value) ||
        !minic_const_value_is_zero(
            context->body->program, context->target, &value, &is_zero)) {
        return false;
    }
    return !is_zero;
}

MinicCoreLowerStatus
minic_core_lower_loop_cfg(MinicCoreLowerContext *context,
            const MinicStatement *statement,
            MinicStatementId continue_label_statement,
            bool *terminated) {
    const MinicBlock *body_source;
    const MinicBlock *iteration_source;
    const MinicExpression *condition_expression;
    const MinicStatement *for_update;
    MinicBlock normalized_for_body;
    MinicBlock scoped_iteration_body;
    MinicBlock normalized_do_while_body;
    MinicStatementId normalized_do_while_continue;
    MinicStatementId normalized_for_continue;
    MinicCoreBlockId body_block;
    MinicCoreBlockId condition_block;
    MinicCoreBlockId exit_block;
    MinicCoreBlockId preheader_block;
    MinicCoreBlockId update_block;
    MinicCoreBlockId saved_break_target;
    MinicCoreLowerStatus status;
    bool body_terminated;
    bool normalized_for;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || statement == NULL || terminated == NULL ||
        statement->kind != MINIC_STATEMENT_WHILE || !minic_core_loop_cleanup_edge_is_empty(statement) ||
        statement->then_block == MINIC_BLOCK_INVALID ||
        statement->else_block != MINIC_BLOCK_INVALID) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    body_source = minic_c0_program_block(context->body->program, statement->then_block);
    if (body_source == NULL) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    condition_expression = NULL;
    if (statement->expression != MINIC_EXPRESSION_INVALID) {
        condition_expression =
            minic_c0_program_expression(context->body->program, statement->expression);
        if (condition_expression == NULL ||
            (!minic_type_is_integer(condition_expression->type) &&
             !minic_type_is_pointer(condition_expression->type))) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
    }
    normalized_do_while_continue = MINIC_STATEMENT_INVALID;
    if (minic_core_loop_normalized_do_while_zero_body(context,
                                      statement,
                                      body_source,
                                      &normalized_do_while_body,
                                      &normalized_do_while_continue)) {
        if (!minic_core_loop_normalized_do_while_block_needs_exit(context,
                                                  &normalized_do_while_body,
                                                  normalized_do_while_continue,
                                                  true)) {
            /* Keep the previously proven flattening semantics when BODY has no
               transfer owned by this loop. This avoids manufacturing CFG state
               for the many effect-only do-while(0) macros already accepted. */
            status = minic_core_lower_internal_lower_block(context, &normalized_do_while_body, &body_terminated);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            *terminated = body_terminated;
            return MINIC_CORE_LOWER_OK;
        }
        {
            MinicCoreBlockId single_iteration_exit;

            if (normalized_do_while_continue >= context->statement_block_count ||
                context->statement_blocks == NULL ||
                context->statement_blocks[normalized_do_while_continue] !=
                    MINIC_CORE_BLOCK_INVALID ||
                !minic_core_function_add_block(context->function, &single_iteration_exit)) {
                return MINIC_CORE_LOWER_ERROR;
            }
            context->statement_blocks[normalized_do_while_continue] = single_iteration_exit;
            saved_break_target = context->break_target;
            context->break_target = single_iteration_exit;
            status = minic_core_lower_internal_lower_block(context, &normalized_do_while_body, &body_terminated);
            context->break_target = saved_break_target;
            if (status != MINIC_CORE_LOWER_OK) {
                (void)fprintf(stderr,
                              "CORE_FAST_TRACE stage=do-while-zero reason=body function=%s "
                              "status=%d span=%zu:%zu\n",
                              context->source_function->name,
                              (int)status,
                              statement->span.begin.line,
                              statement->span.begin.column);
                return status;
            }
            if (!body_terminated) {
                status = minic_core_lower_internal_set_branch(
                    context, context->block_id, statement->span, single_iteration_exit);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
            context->block_id = single_iteration_exit;
            *terminated = false;
            return MINIC_CORE_LOWER_OK;
        }
    }

    iteration_source = body_source;
    for_update = NULL;
    normalized_for = false;
    normalized_for_continue = MINIC_STATEMENT_INVALID;
    if (minic_core_loop_normalized_for_update_tail(
            context, statement, body_source, &normalized_for_body, &for_update)) {
        iteration_source = &normalized_for_body;
        normalized_for = true;
    } else if (minic_core_loop_normalized_for_continue_tail(
                   context, statement, body_source, &normalized_for_body)) {
        iteration_source = &normalized_for_body;
        normalized_for = true;
    }
    if (!normalized_for &&
        minic_core_loop_normalized_do_while_true_body(context,
                                      statement,
                                      body_source,
                                      &normalized_do_while_body,
                                      &normalized_do_while_continue)) {
        if (continue_label_statement != MINIC_STATEMENT_INVALID &&
            continue_label_statement != normalized_do_while_continue) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        continue_label_statement = normalized_do_while_continue;
        iteration_source = &normalized_do_while_body;
    }
    /* M149_PARSER_TAIL_CONTINUE_PROVENANCE_OWNER: minic_core_loop_normalized_for_update_tail
       and minic_core_loop_normalized_for_continue_tail have already proven the exact parser
       shape and same-span synthetic LABEL at the source for-loop tail.  Use
       that statement id directly; never recover identity by scanning GOTOs.
       If the adjacent-label path also supplied an id, require exact agreement.
       This keeps ordinary while ownership on its pre-M137 adjacent-label path
       while giving detached normalized-for views one structural provenance. */
    if (normalized_for) {
        size_t continue_tail_distance = for_update != NULL ? 2U : 1U;

        if (body_source->statement_count < continue_tail_distance) {
            return MINIC_CORE_LOWER_ERROR;
        }
        normalized_for_continue =
            body_source->statements[body_source->statement_count - continue_tail_distance];
        if (normalized_for_continue == MINIC_STATEMENT_INVALID ||
            normalized_for_continue >= context->statement_block_count) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (continue_label_statement != MINIC_STATEMENT_INVALID &&
            continue_label_statement != normalized_for_continue) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        continue_label_statement = normalized_for_continue;
    }
    /* M141_SCOPED_LOOP_CONTINUE_TAIL_OWNER: M137 has already proven that
       continue_label_statement is the unique parser-owned continue target for
       this exact loop and lower_while will bind it to condition_block before
       lowering the body. If the normalized-for view still retains that exact
       synthetic label as its sequential tail, remove only that statement from
       the executable iteration view. This prevents generic LABEL lowering from
       switching context->block_id back to the already-terminated condition
       block and falsely clearing body termination. Ordinary labels and loops
       without an M137-proven target are bit-for-bit unchanged. */
    if (normalized_for && continue_label_statement != MINIC_STATEMENT_INVALID &&
        iteration_source != NULL && iteration_source->statement_count > 0U &&
        iteration_source->statements[iteration_source->statement_count - 1U] ==
            continue_label_statement) {
        scoped_iteration_body = *iteration_source;
        scoped_iteration_body.statement_count -= 1U;
        iteration_source = &scoped_iteration_body;
    }
    if (statement->expression == MINIC_EXPRESSION_INVALID && !normalized_for) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }

    preheader_block = context->block_id;
    update_block = MINIC_CORE_BLOCK_INVALID;
    if (!minic_core_function_add_block(context->function, &condition_block) ||
        !minic_core_function_add_block(context->function, &body_block) ||
        (normalized_for && for_update != NULL &&
         continue_label_statement != MINIC_STATEMENT_INVALID &&
         !minic_core_function_add_block(context->function, &update_block)) ||
        !minic_core_function_add_block(context->function, &exit_block)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    /* M147_NORMALIZED_FOR_CONTINUE_BINDING_OWNER: M145/M146 proved that the
       same-span detached-label heuristic must not mutate an ordinary source
       WHILE CFG.  Keep binding at the established post-block-allocation point,
       but require parser-normalized `for` provenance first. */
    if (continue_label_statement != MINIC_STATEMENT_INVALID) {
        if (context->statement_blocks == NULL ||
            continue_label_statement >= context->statement_block_count ||
            context->statement_blocks[continue_label_statement] != MINIC_CORE_BLOCK_INVALID) {
            return MINIC_CORE_LOWER_ERROR;
        }
        /* M148_NORMALIZED_FOR_UPDATE_CONTINUE_CFG_OWNER: C `continue` in a
           source for-loop executes the iteration expression before condition
           re-evaluation.  A no-update/unbounded for therefore targets the
           condition block directly, while an update-bearing for with a proven
           continue gets a dedicated update block.  Loops without a recovered
           continue keep the historical inline-update lowering unchanged. */
        context->statement_blocks[continue_label_statement] =
            for_update != NULL ? update_block : condition_block;
    }
    status = minic_core_lower_internal_set_branch(context, preheader_block, statement->span, condition_block);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    context->block_id = condition_block;
    if (statement->expression == MINIC_EXPRESSION_INVALID ||
        core_loop_condition_is_constant_true(context, statement->expression)) {
        /* C defines an omitted for-condition as true, and a proven nonzero
           integer constant has the same CFG reachability. Keep an explicit
           Core condition block so break/backedge ownership remains identical. */
        status = minic_core_lower_internal_set_branch(context, condition_block, statement->span, body_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    } else {
        /* M84_SHARED_LOOP_CONDITION_BRANCH: if/while/normalized-for share one
           scalar-condition owner. This admits pointer truth values and keeps
           !/&&/|| short-circuit CFG construction out of the loop lowering. */
        status = minic_core_lower_internal_lower_condition_branch(context,
                                        statement->expression,
                                        statement->span,
                                        body_block,
                                        exit_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    }

    context->block_id = body_block;
    saved_break_target = context->break_target;
    context->break_target = exit_block;
    status = minic_core_lower_internal_lower_block(context, iteration_source, &body_terminated);
    context->break_target = saved_break_target;
    if (status != MINIC_CORE_LOWER_OK) {
        (void)fprintf(stderr,
                      "CORE_FAST_TRACE stage=while reason=body function=%s status=%d "
                      "normalized_for=%d has_update=%d span=%zu:%zu\n",
                      context->source_function->name,
                      (int)status,
                      normalized_for ? 1 : 0,
                      for_update != NULL ? 1 : 0,
                      statement->span.begin.line,
                      statement->span.begin.column);
        return status;
    }
    if (for_update != NULL && update_block != MINIC_CORE_BLOCK_INVALID) {
        /* M148_NORMALIZED_FOR_UPDATE_CONTINUE_CFG_OWNER: converge both natural
           body fallthrough and every continue edge at one update owner, then
           evaluate the update exactly once before the condition backedge. */
        if (!body_terminated) {
            status = minic_core_lower_internal_set_branch(context, context->block_id, statement->span, update_block);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
        context->block_id = update_block;
        status = minic_core_lower_internal_lower_expression_statement(context, for_update);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (context->block_id < context->function->block_count &&
            context->function->blocks[context->block_id].has_terminator) {
            body_terminated = true;
        } else {
            body_terminated = false;
            status = minic_core_lower_internal_set_branch(context, context->block_id, statement->span, condition_block);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
    } else {
        if (!body_terminated && for_update != NULL) {
            status = minic_core_lower_internal_lower_expression_statement(context, for_update);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            if (context->block_id < context->function->block_count &&
                context->function->blocks[context->block_id].has_terminator) {
                body_terminated = true;
            }
        }
        if (!body_terminated) {
            status = minic_core_lower_internal_set_branch(context, context->block_id, statement->span, condition_block);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
    }
    context->block_id = exit_block;
    /* M133_CONSTANT_TRUE_LOOP_REACHABILITY: reachability, not syntax spelling,
       owns loop fallthrough. Normal variable/false-capable conditions already
       contribute a false edge; omitted and constant-true conditions do not. */
    if (!minic_core_lower_internal_block_has_predecessor(context->function, exit_block)) {
        MinicCoreTerminator exit_terminator;

        (void)memset(&exit_terminator, 0, sizeof(exit_terminator));
        exit_terminator.kind = MINIC_CORE_TERMINATOR_UNREACHABLE;
        exit_terminator.span = statement->span;
        exit_terminator.return_value = MINIC_CORE_VALUE_INVALID;
        exit_terminator.return_object = MINIC_CORE_OBJECT_INVALID;
        if (!minic_core_function_set_terminator(
                context->function, exit_block, &exit_terminator)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        *terminated = true;
        return MINIC_CORE_LOWER_OK;
    }
    *terminated = false;
    return MINIC_CORE_LOWER_OK;
}

/* MUTABLE_SPECIALIZATION_LOOP_BARRIER: do not substitute an incoming
   specialized parameter constant for a variable updated within the loop. */
static void core_invalidate_loop_mutated_specialization_facts(
    MinicCoreLowerContext *context, const MinicStatement *loop_statement,
    const unsigned char *modified) {
    size_t index;

    if (context == NULL || context->source_function == NULL ||
        context->local_integer_constants == NULL || context->body == NULL ||
        loop_statement == NULL || loop_statement->kind != MINIC_STATEMENT_WHILE) {
        return;
    }
    for (index = 0U; index < context->source_function->local_count; ++index) {
        MinicLocalId local_id = context->source_function->local_begin + index;
        const MinicCoreLocalIntegerConstant *fact =
            &context->local_integer_constants[index];
        bool may_mutate;
        if (!fact->specialization_known || fact->specialization_modified) {
            continue;
        }
        /* NULL means the whole-subtree collector could not allocate its
         * mask, so every source local is conservatively modified. */
        may_mutate = modified == NULL || modified[index] != 0U;
        if (may_mutate) {
            minic_core_local_fact_invalidate(context, local_id);
            if (getenv("MINIC_LOOP_MUTATED_FACT_TRACE") != NULL) {
                (void)fprintf(stderr,
                              "LOOP_SPECIALIZATION_FACT_KILL function=%s local=%zu\n",
                              context->source_function->name != NULL
                                  ? context->source_function->name : "?",
                              (size_t)local_id);
            }
        }
    }
}

static void core_restore_loop_invariant_facts(
    MinicCoreLowerContext *context,
    const MinicCoreLocalIntegerConstant *incoming,
    const MinicStatement *loop_statement,
    const unsigned char *modified) {
    size_t index;

    if (context == NULL || context->source_function == NULL ||
        context->local_integer_constants == NULL || incoming == NULL ||
        loop_statement == NULL || loop_statement->kind != MINIC_STATEMENT_WHILE) {
        return;
    }
    for (index = 0U; index < context->source_function->local_count; ++index) {
        if (!incoming[index].known || incoming[index].escaped ||
            context->local_integer_constants[index].escaped ||
            modified == NULL || modified[index] != 0U) {
            continue;
        }
        context->local_integer_constants[index] = incoming[index];
    }
}

/* Source AST effects are independent of the facts changed while lowering
 * the loop. Gather condition and body writes exactly once, then apply that
 * same conservative summary at both entry and exit barriers. */
static unsigned char *core_collect_loop_modified_locals(
    const MinicCoreLowerContext *context, const MinicStatement *statement,
    const MinicBlock *body) {
    unsigned char *modified;
    size_t count;

    if (context == NULL || context->source_function == NULL ||
        statement == NULL) return NULL;
    count = context->source_function->local_count;
    modified = (unsigned char *)calloc(count == 0U ? 1U : count,
                                       sizeof(*modified));
    if (modified == NULL) return NULL;
    if (body == NULL) {
        if (count != 0U) (void)memset(modified, 1, count);
        return modified;
    }
    if (statement->expression != MINIC_EXPRESSION_INVALID)
        core_expression_collect_modified_locals(
            context, statement->expression, modified, 0U);
    core_block_collect_modified_locals(context, body, modified, 0U);
    return modified;
}

/* One owner for normal loop facts. Parser-created while/for label pairs use
 * minic_core_lower_loop_cfg directly because their fact snapshots are owned
 * by the normalized source path. */
MinicCoreLowerStatus minic_core_lower_loop_statement(
    MinicCoreLowerContext *context, const MinicStatement *statement,
    bool *terminated) {
    MinicCoreLowerStatus status;
    if (context == NULL || statement == NULL || terminated == NULL)
        return MINIC_CORE_LOWER_ERROR;
    const MinicBlock *local_fact_loop_body;
    MinicBlock local_fact_single_iteration_body;
    MinicStatementId local_fact_continue_label;
    MinicCoreLocalIntegerConstant *loop_incoming_facts;
    unsigned char *loop_modified_locals = NULL;
    bool local_fact_single_pass;

    loop_incoming_facts = minic_core_local_facts_snapshot(context);
    if (context->source_function != NULL &&
        context->source_function->local_count != 0U &&
        loop_incoming_facts == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }

    local_fact_loop_body = minic_c0_program_block(
        context->body->program, statement->then_block);
    local_fact_continue_label = MINIC_STATEMENT_INVALID;
    local_fact_single_pass =
        local_fact_loop_body != NULL &&
        minic_core_loop_normalized_do_while_zero_body(context,
                                      statement,
                                      local_fact_loop_body,
                                      &local_fact_single_iteration_body,
                                      &local_fact_continue_label) &&
        !minic_core_loop_normalized_do_while_block_needs_exit(context,
                                              &local_fact_single_iteration_body,
                                              local_fact_continue_label,
                                              true);
    if (!local_fact_single_pass) {
        loop_modified_locals = core_collect_loop_modified_locals(
            context, statement, local_fact_loop_body);
        /* Loop-body changes invalidate specialized parameters
           before the initial condition and later backedges. */
        core_invalidate_loop_mutated_specialization_facts(
            context, statement, loop_modified_locals);
        minic_core_local_facts_clear_known(context);
        core_restore_loop_invariant_facts(
            context, loop_incoming_facts, statement, loop_modified_locals);
    }
    status = minic_core_lower_loop_cfg(
        context, statement, MINIC_STATEMENT_INVALID, terminated);
    if (!local_fact_single_pass) {
        minic_core_local_facts_clear_known(context);
        core_restore_loop_invariant_facts(
            context, loop_incoming_facts, statement, loop_modified_locals);
    }
    free(loop_modified_locals);
    free(loop_incoming_facts);
    return status;
}
