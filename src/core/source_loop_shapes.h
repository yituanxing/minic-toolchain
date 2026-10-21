#ifndef MINIC_CORE_SOURCE_LOOP_SHAPES_H
#define MINIC_CORE_SOURCE_LOOP_SHAPES_H

#include "core/core_lower_internal.h"

/* Read-only recognition of AST forms synthesized by parse_for/do_while.
 * The AST is immutable; this module returns bounded source-block views
 * and must never introduce speculative CFG edges or change cleanup scope.
 *
 * These are source language / frontend shape queries, NOT target Machine IR
 * or register/frame placement. All four backends consume the same Core CFG.
 */
bool minic_core_loop_internal_while_label_pair(const MinicStatement *label, const MinicStatement *loop);

bool minic_core_loop_cleanup_edge_is_empty(const MinicStatement *statement);

bool minic_core_loop_normalized_do_while_zero_body(const MinicCoreLowerContext *context,
                                          const MinicStatement *loop,
                                          const MinicBlock *body,
                                          MinicBlock *single_iteration_body,
                                          MinicStatementId *continue_label_statement);

bool minic_core_loop_normalized_do_while_block_needs_exit(
    const MinicCoreLowerContext *context,
    const MinicBlock *block,
    MinicStatementId continue_label_statement,
    bool break_targets_outer_loop);

bool minic_core_loop_normalized_do_while_true_body(const MinicCoreLowerContext *context,
                                          const MinicStatement *loop,
                                          const MinicBlock *body,
                                          MinicBlock *iteration_body,
                                          MinicStatementId *continue_label_statement);

bool minic_core_loop_normalized_for_continue_tail(const MinicCoreLowerContext *context,
                                         const MinicStatement *loop,
                                         const MinicBlock *body,
                                         MinicBlock *iteration_body);

bool minic_core_loop_normalized_for_update_tail(const MinicCoreLowerContext *context,
                                       const MinicStatement *loop,
                                       const MinicBlock *body,
                                       MinicBlock *iteration_body,
                                       const MinicStatement **update_statement);

#endif
