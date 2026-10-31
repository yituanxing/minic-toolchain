#ifndef MINIC_CORE_OPT_PURE_STATEMENT_H
#define MINIC_CORE_OPT_PURE_STATEMENT_H

#include "core/reentry_index.h"
#include "frontend/const_eval.h"
#include <stdbool.h>

/* Optimization-only AST proof. A constant value is never grounds for erasing
 * a GNU statement expression unless every reachable prefix is effect-free.
 * Core can supply its richer pure-constant facts; the inline-specializer uses
 * the frontend constant evaluator. Neither changes C ICE admission rules. */
typedef bool (*MinicOptPureGuardTruth)(void *context,
                                      MinicExpressionId condition_id,
                                      bool *truth);

/* Parser's GNU do { BUILD_BUG_ON(CONST_FALSE); } while (0) has one exact
 * normalized source shape: WHILE(true), IF(false), synthetic LABEL, IF(true)
 * BREAK. Prove zero effect AND no jump/asm-goto/label-address reentry. */
static inline bool minic_opt_pure_guard_loop(
    const MinicC0Program *program, MinicCoreReentryIndex *reentry_index,
    const MinicStatement *loop, void *context, MinicOptPureGuardTruth truth) {
    const MinicBlock *body;
    const MinicBlock *dead_block;
    const MinicBlock *break_block;
    const MinicStatement *dead_guard;
    const MinicStatement *loop_label;
    const MinicStatement *loop_exit;
    const MinicStatement *dead_statement;
    const MinicStatement *break_statement;
    MinicCoreReentryQuery query;
    bool result;

    if (program == NULL || loop == NULL || truth == NULL ||
        loop->kind != MINIC_STATEMENT_WHILE ||
        loop->then_block == MINIC_BLOCK_INVALID ||
        loop->else_block != MINIC_BLOCK_INVALID ||
        loop->expression == MINIC_EXPRESSION_INVALID ||
        loop->cleanup_context != loop->cleanup_stop_context ||
        !truth(context, loop->expression, &result) || !result) {
        return false;
    }
    body = minic_c0_program_block(program, loop->then_block);
    if (body == NULL || body->statement_count != 3U) return false;
    dead_guard = minic_c0_program_statement(program, body->statements[0]);
    loop_label = minic_c0_program_statement(program, body->statements[1]);
    loop_exit = minic_c0_program_statement(program, body->statements[2]);
    if (dead_guard == NULL || loop_label == NULL || loop_exit == NULL ||
        dead_guard->kind != MINIC_STATEMENT_IF ||
        loop_label->kind != MINIC_STATEMENT_LABEL ||
        loop_exit->kind != MINIC_STATEMENT_IF ||
        dead_guard->cleanup_context != dead_guard->cleanup_stop_context ||
        loop_label->cleanup_context != loop_label->cleanup_stop_context ||
        loop_exit->cleanup_context != loop_exit->cleanup_stop_context ||
        dead_guard->else_block != MINIC_BLOCK_INVALID ||
        loop_exit->else_block != MINIC_BLOCK_INVALID ||
        dead_guard->then_block == MINIC_BLOCK_INVALID ||
        loop_exit->then_block == MINIC_BLOCK_INVALID ||
        !truth(context, dead_guard->expression, &result) || result ||
        !truth(context, loop_exit->expression, &result) || !result) {
        return false;
    }
    dead_block = minic_c0_program_block(program, dead_guard->then_block);
    break_block = minic_c0_program_block(program, loop_exit->then_block);
    if (dead_block == NULL || dead_block->statement_count != 1U ||
        break_block == NULL || break_block->statement_count != 1U) return false;
    dead_statement = minic_c0_program_statement(program, dead_block->statements[0]);
    break_statement = minic_c0_program_statement(program, break_block->statements[0]);
    if (dead_statement == NULL ||
        dead_statement->kind != MINIC_STATEMENT_EXPRESSION ||
        dead_statement->cleanup_context != dead_statement->cleanup_stop_context ||
        break_statement == NULL ||
        break_statement->kind != MINIC_STATEMENT_BREAK ||
        break_statement->cleanup_context != break_statement->cleanup_stop_context) {
        return false;
    }
    query.membership = NULL;
    query.label_id = body->statements[1];
    query.checks = MINIC_CORE_REENTRY_CHECK_EXTERNAL_JUMP |
                   MINIC_CORE_REENTRY_CHECK_LABEL_ADDRESS;
    query.purpose = MINIC_CORE_REENTRY_PURPOSE_LOOP_LABEL;
    return minic_core_reentry_query(reentry_index, program, &query) == 0U;
}

/* Prove a straight sequence of the above no-op guards and nested discarded
 * pure statement expressions. Dynamic calls, writes, volatile accesses, ASM,
 * GOTOs, and unrecognized loops all fail closed. */
static inline bool minic_opt_pure_statement_block(
    const MinicC0Program *program, const MinicTargetInfo *target,
    MinicCoreReentryIndex *reentry_index, MinicBlockId block_id,
    void *context, MinicOptPureGuardTruth truth, unsigned int depth) {
    const MinicBlock *block;
    size_t index;
    if (program == NULL || target == NULL || truth == NULL ||
        block_id == MINIC_BLOCK_INVALID || depth > 12U) return false;
    block = minic_c0_program_block(program, block_id);
    if (block == NULL || block->statement_count > 256U) return false;
    for (index = 0U; index < block->statement_count; ++index) {
        const MinicStatement *statement = minic_c0_program_statement(
            program, block->statements[index]);
        const MinicExpression *expression;
        MinicConstValue ignored;
        if (statement == NULL ||
            statement->cleanup_context != statement->cleanup_stop_context)
            return false;
        if (statement->kind == MINIC_STATEMENT_WHILE) {
            if (!minic_opt_pure_guard_loop(program, reentry_index, statement,
                                           context, truth)) return false;
            continue;
        }
        if (statement->kind != MINIC_STATEMENT_EXPRESSION ||
            statement->expression == MINIC_EXPRESSION_INVALID) return false;
        expression = minic_c0_program_expression(program, statement->expression);
        if (expression == NULL) return false;
        if (expression->kind == MINIC_EXPRESSION_STATEMENT) {
            const MinicExpression *result = minic_c0_program_expression(
                program, expression->value.statement_expression.result);
            if (!minic_opt_pure_statement_block(
                    program, target, reentry_index,
                    expression->value.statement_expression.block,
                    context, truth, depth + 1U) ||
                (result != NULL && !minic_const_eval_integer(
                     program, target, expression->value.statement_expression.result,
                     &ignored))) return false;
            continue;
        }
        if (expression->kind == MINIC_EXPRESSION_DISCARD) {
            if (!minic_const_eval_integer(
                    program, target, expression->value.unary.operand, &ignored))
                return false;
            continue;
        }
        if (!minic_type_is_integer(expression->type) ||
            !minic_const_eval_integer(program, target, statement->expression,
                                      &ignored)) return false;
    }
    return true;
}

#endif
