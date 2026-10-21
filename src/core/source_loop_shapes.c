#include "core/source_loop_shapes.h"

/* M121/M159 normalized-do and M147-M149 normalized-for provenance rules.
 * Extracted unchanged: parser-owned synthetic labels, cleanup contexts,
 * and source positions are necessary to avoid false CFG predecessor edges.
 * Deliberately retain the original fail-closed AST tests and recursion. */
bool minic_core_loop_internal_while_label_pair(const MinicStatement *label, const MinicStatement *loop) {
    bool same_begin;

    if (label == NULL || loop == NULL) {
        return false;
    }
    same_begin = label->span.begin.offset == loop->span.begin.offset &&
                 label->span.begin.line == loop->span.begin.line &&
                 label->span.begin.column == loop->span.begin.column;
    return label->kind == MINIC_STATEMENT_LABEL && loop->kind == MINIC_STATEMENT_WHILE &&
           same_begin && label->target_expression == MINIC_EXPRESSION_INVALID &&
           label->expression == MINIC_EXPRESSION_INVALID &&
           label->target_statement == MINIC_STATEMENT_INVALID;
}

static bool source_position_equal(MinicSourcePosition left, MinicSourcePosition right) {
    return left.offset == right.offset && left.line == right.line && left.column == right.column;
}

bool minic_core_loop_cleanup_edge_is_empty(const MinicStatement *statement) {
    return statement != NULL &&
           statement->cleanup_context == statement->cleanup_stop_context;
}

/* M121_SELECTIVE_DO_WHILE_ZERO_EXIT_CFG: parse_do_while lowers source
   `do BODY while (0)` into a synthetic while(1) tail. Preserve the historical
   flattening path when BODY has no control transfer to this loop; only build a
   real exit block when a source break/continue actually needs loop ownership. */
bool minic_core_loop_normalized_do_while_zero_body(const MinicCoreLowerContext *context,
                                          const MinicStatement *loop,
                                          const MinicBlock *body,
                                          MinicBlock *single_iteration_body,
                                          MinicStatementId *continue_label_statement) {
    const MinicExpression *loop_condition;
    const MinicExpression *negated_condition;
    const MinicExpression *source_condition;
    const MinicStatement *continue_label;
    const MinicStatement *condition_check;
    const MinicStatement *break_statement;
    const MinicBlock *break_block;
    MinicStatementId continue_id;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        loop == NULL || body == NULL || single_iteration_body == NULL ||
        continue_label_statement == NULL || body->statement_count < 2U) {
        return false;
    }
    loop_condition = minic_c0_program_expression(context->body->program, loop->expression);
    continue_id = body->statements[body->statement_count - 2U];
    continue_label = minic_c0_program_statement(context->body->program, continue_id);
    condition_check = minic_c0_program_statement(
        context->body->program, body->statements[body->statement_count - 1U]);
    if (loop_condition == NULL || loop_condition->kind != MINIC_EXPRESSION_INTEGER ||
        !minic_type_is_integer(loop_condition->type) || loop_condition->value.integer_value != 1 ||
        continue_label == NULL || continue_label->kind != MINIC_STATEMENT_LABEL ||
        continue_label->target_expression != MINIC_EXPRESSION_INVALID ||
        continue_label->expression != MINIC_EXPRESSION_INVALID ||
        continue_label->target_statement != MINIC_STATEMENT_INVALID ||
        !source_position_equal(continue_label->span.begin, loop->span.begin) ||
        condition_check == NULL || condition_check->kind != MINIC_STATEMENT_IF ||
        condition_check->expression == MINIC_EXPRESSION_INVALID ||
        condition_check->then_block == MINIC_BLOCK_INVALID ||
        condition_check->else_block != MINIC_BLOCK_INVALID ||
        !source_position_equal(condition_check->span.begin, loop->span.begin)) {
        return false;
    }
    negated_condition =
        minic_c0_program_expression(context->body->program, condition_check->expression);
    if (negated_condition == NULL || negated_condition->kind != MINIC_EXPRESSION_UNARY ||
        negated_condition->value.unary.operator_kind != MINIC_UNARY_LOGICAL_NOT) {
        return false;
    }
    source_condition = minic_c0_program_expression(
        context->body->program, negated_condition->value.unary.operand);
    if (source_condition == NULL || source_condition->kind != MINIC_EXPRESSION_INTEGER ||
        !minic_type_is_integer(source_condition->type) || source_condition->value.integer_value != 0) {
        return false;
    }
    break_block = minic_c0_program_block(context->body->program, condition_check->then_block);
    if (break_block == NULL || break_block->statement_count != 1U) {
        return false;
    }
    break_statement = minic_c0_program_statement(context->body->program, break_block->statements[0]);
    if (break_statement == NULL || break_statement->kind != MINIC_STATEMENT_BREAK ||
        !minic_core_loop_cleanup_edge_is_empty(break_statement) ||
        !source_position_equal(break_statement->span.begin, loop->span.begin)) {
        return false;
    }

    *single_iteration_body = *body;
    single_iteration_body->statement_count -= 2U;
    *continue_label_statement = continue_id;
    return true;
}

bool minic_core_loop_normalized_do_while_block_needs_exit(
    const MinicCoreLowerContext *context,
    const MinicBlock *block,
    MinicStatementId continue_label_statement,
    bool break_targets_outer_loop) {
    size_t index;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        block == NULL) {
        return true;
    }
    for (index = 0U; index < block->statement_count; ++index) {
        const MinicStatement *statement = minic_c0_program_statement(
            context->body->program, block->statements[index]);
        if (statement == NULL) {
            return true;
        }
        if (break_targets_outer_loop && statement->kind == MINIC_STATEMENT_BREAK) {
            return true;
        }
        if (statement->kind == MINIC_STATEMENT_GOTO &&
            statement->target_statement == continue_label_statement) {
            return true;
        }
        if (statement->kind == MINIC_STATEMENT_IF ||
            statement->kind == MINIC_STATEMENT_WHILE ||
            statement->kind == MINIC_STATEMENT_SWITCH) {
            const MinicBlock *then_block = statement->then_block == MINIC_BLOCK_INVALID
                                                ? NULL
                                                : minic_c0_program_block(
                                                      context->body->program,
                                                      statement->then_block);
            const MinicBlock *else_block = statement->else_block == MINIC_BLOCK_INVALID
                                                ? NULL
                                                : minic_c0_program_block(
                                                      context->body->program,
                                                      statement->else_block);
            bool nested_break_targets_outer =
                statement->kind == MINIC_STATEMENT_IF ? break_targets_outer_loop : false;
            if (statement->then_block != MINIC_BLOCK_INVALID &&
                (then_block == NULL || minic_core_loop_normalized_do_while_block_needs_exit(
                                           context,
                                           then_block,
                                           continue_label_statement,
                                           nested_break_targets_outer))) {
                return true;
            }
            if (statement->else_block != MINIC_BLOCK_INVALID &&
                (else_block == NULL || minic_core_loop_normalized_do_while_block_needs_exit(
                                           context,
                                           else_block,
                                           continue_label_statement,
                                           nested_break_targets_outer))) {
                return true;
            }
        }
    }
    return false;
}

/* M78_OMITTED_FOR_CONDITION: parse_for represents a missing source
   condition as MINIC_EXPRESSION_INVALID and appends its synthetic continue
   label to the loop body. Recognize the no-update variant explicitly so Core
   can distinguish `for (;;)` from an invalid source while statement. */
/* M159_CONSTANT_TRUE_DO_WHILE_REACHABILITY_OWNER: parse_do_while lowers
   every source do/while into an outer synthetic while(1), followed inside its
   body by a synthetic continue label and `if (!source_condition) break`.
   When source_condition is a compile-time nonzero integer and the original
   body contains no break/continue edge that needs those synthetic nodes, that
   tail is unreachable control metadata.  Strip it from the executable view so
   the ordinary constant-true while CFG has no false exit predecessor. */
bool minic_core_loop_normalized_do_while_true_body(const MinicCoreLowerContext *context,
                                          const MinicStatement *loop,
                                          const MinicBlock *body,
                                          MinicBlock *iteration_body,
                                          MinicStatementId *continue_label_statement) {
    const MinicExpression *loop_condition;
    const MinicExpression *negated_condition;
    const MinicExpression *source_condition;
    const MinicStatement *continue_label;
    const MinicStatement *condition_check;
    const MinicStatement *break_statement;
    const MinicBlock *break_block;
    MinicConstValue condition_value;
    MinicStatementId continue_id;
    bool is_zero;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->target == NULL || loop == NULL || body == NULL || iteration_body == NULL ||
        continue_label_statement == NULL || body->statement_count < 2U) {
        return false;
    }
    loop_condition = minic_c0_program_expression(context->body->program, loop->expression);
    continue_id = body->statements[body->statement_count - 2U];
    continue_label = minic_c0_program_statement(context->body->program, continue_id);
    condition_check = minic_c0_program_statement(
        context->body->program, body->statements[body->statement_count - 1U]);
    if (loop_condition == NULL || loop_condition->kind != MINIC_EXPRESSION_INTEGER ||
        !minic_type_is_integer(loop_condition->type) || loop_condition->value.integer_value != 1 ||
        continue_label == NULL || continue_label->kind != MINIC_STATEMENT_LABEL ||
        continue_label->target_expression != MINIC_EXPRESSION_INVALID ||
        continue_label->expression != MINIC_EXPRESSION_INVALID ||
        continue_label->target_statement != MINIC_STATEMENT_INVALID ||
        !source_position_equal(continue_label->span.begin, loop->span.begin) ||
        condition_check == NULL || condition_check->kind != MINIC_STATEMENT_IF ||
        condition_check->expression == MINIC_EXPRESSION_INVALID ||
        condition_check->then_block == MINIC_BLOCK_INVALID ||
        condition_check->else_block != MINIC_BLOCK_INVALID ||
        !source_position_equal(condition_check->span.begin, loop->span.begin)) {
        return false;
    }
    negated_condition =
        minic_c0_program_expression(context->body->program, condition_check->expression);
    if (negated_condition == NULL || negated_condition->kind != MINIC_EXPRESSION_UNARY ||
        negated_condition->value.unary.operator_kind != MINIC_UNARY_LOGICAL_NOT) {
        return false;
    }
    source_condition = minic_c0_program_expression(
        context->body->program, negated_condition->value.unary.operand);
    if (source_condition == NULL || !minic_type_is_integer(source_condition->type) ||
        !minic_const_eval_integer(context->body->program,
                                 context->target,
                                 negated_condition->value.unary.operand,
                                 &condition_value) ||
        !minic_const_value_is_zero(context->body->program,
                                   context->target,
                                   &condition_value,
                                   &is_zero) ||
        is_zero) {
        return false;
    }
    break_block = minic_c0_program_block(context->body->program, condition_check->then_block);
    if (break_block == NULL || break_block->statement_count != 1U) {
        return false;
    }
    break_statement = minic_c0_program_statement(context->body->program, break_block->statements[0]);
    if (break_statement == NULL || break_statement->kind != MINIC_STATEMENT_BREAK ||
        !minic_core_loop_cleanup_edge_is_empty(break_statement) ||
        !source_position_equal(break_statement->span.begin, loop->span.begin)) {
        return false;
    }

    /* The source condition is proven nonzero, so the parser's trailing
       `if (!condition) break` is unreachable. Preserve source continue
       semantics by binding the synthetic continue label to the real loop
       condition block instead of retaining the synthetic tail. Source break
       statements remain ordinary loop-owned edges to exit_block. */
    *iteration_body = *body;
    iteration_body->statement_count -= 2U;
    *continue_label_statement = continue_id;
    return true;
}

bool minic_core_loop_normalized_for_continue_tail(const MinicCoreLowerContext *context,
                                         const MinicStatement *loop,
                                         const MinicBlock *body,
                                         MinicBlock *iteration_body) {
    const MinicStatement *continue_label;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        loop == NULL || body == NULL || iteration_body == NULL || body->statement_count < 1U) {
        return false;
    }
    continue_label = minic_c0_program_statement(
        context->body->program, body->statements[body->statement_count - 1U]);
    if (continue_label == NULL || continue_label->kind != MINIC_STATEMENT_LABEL ||
        continue_label->target_expression != MINIC_EXPRESSION_INVALID ||
        continue_label->expression != MINIC_EXPRESSION_INVALID ||
        continue_label->target_statement != MINIC_STATEMENT_INVALID ||
        !source_position_equal(continue_label->span.begin, loop->span.begin)) {
        return false;
    }
    /* A normalized for-loop's synthetic continue label is part of the
       iteration body only when reaching it does not cross a cleanup lifetime.
       Cleanup-bearing scopes keep the historical tail shape until Core owns
       cleanup transitions explicitly. */
    *iteration_body = *body;
    if (!minic_core_loop_cleanup_edge_is_empty(continue_label)) {
        iteration_body->statement_count -= 1U;
    }
    return true;
}

bool minic_core_loop_normalized_for_update_tail(const MinicCoreLowerContext *context,
                                       const MinicStatement *loop,
                                       const MinicBlock *body,
                                       MinicBlock *iteration_body,
                                       const MinicStatement **update_statement) {
    const MinicStatement *continue_label;
    const MinicStatement *update;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        loop == NULL || body == NULL || iteration_body == NULL || update_statement == NULL ||
        body->statement_count < 2U) {
        return false;
    }
    continue_label = minic_c0_program_statement(context->body->program,
                                                body->statements[body->statement_count - 2U]);
    update = minic_c0_program_statement(context->body->program,
                                        body->statements[body->statement_count - 1U]);
    if (continue_label == NULL || continue_label->kind != MINIC_STATEMENT_LABEL ||
        continue_label->target_expression != MINIC_EXPRESSION_INVALID ||
        continue_label->expression != MINIC_EXPRESSION_INVALID ||
        continue_label->target_statement != MINIC_STATEMENT_INVALID ||
        !source_position_equal(continue_label->span.begin, loop->span.begin) || update == NULL ||
        update->kind != MINIC_STATEMENT_EXPRESSION || !minic_core_loop_cleanup_edge_is_empty(update) ||
        update->expression == MINIC_EXPRESSION_INVALID) {
        return false;
    }
    /* The update is always outside the lowered iteration body.  Preserve the
       preceding synthetic continue label only for a zero-distance cleanup
       edge; otherwise leave cleanup-bearing for scopes on the established
       fail-closed path. */
    *iteration_body = *body;
    iteration_body->statement_count -=
        minic_core_loop_cleanup_edge_is_empty(continue_label) ? 1U : 2U;
    *update_statement = update;
    return true;
}

