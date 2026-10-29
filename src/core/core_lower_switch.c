#include "core/core_lower_internal.h"
#include "core/reentry_index.h"
#include "core/source_loop_shapes.h"
#include "core/local_facts.h"
#include "frontend/const_eval.h"
#include "frontend/expression_semantics.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Core switch ownership: source-level dispatch, nested cases, falls-through,
 * synthetic exit reachability and function-label reentry (no target ABI). */

#define MINIC_CORE_SWITCH_LABEL_LIMIT 512U

typedef struct MinicCoreSwitchLabel {
    size_t source_index;
    const MinicStatement *statement;
    MinicCoreBlockId body_block;
    MinicCoreBlockId test_block;
} MinicCoreSwitchLabel;

static MinicCoreLowerStatus append_switch_integer_constant(MinicCoreLowerContext *context,
                                                           MinicSourceSpan span,
                                                           MinicType type,
                                                           int64_t value,
                                                           MinicCoreValueId *value_id) {
    MinicCoreInstruction instruction;

    if (context == NULL || context->function == NULL || value_id == NULL ||
        !minic_type_is_integer(type)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&instruction, 0, sizeof(instruction));
    instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_CONSTANT;
    instruction.span = span;
    instruction.type = type;
    instruction.result = MINIC_CORE_VALUE_INVALID;
    instruction.value.integer_value = value;
    return minic_core_function_append_value_instruction(
               context->function, context->block_id, &instruction, value_id)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

static MinicCoreLowerStatus set_switch_conditional_branch(MinicCoreLowerContext *context,
                                                          MinicSourceSpan span,
                                                          MinicCoreValueId condition,
                                                          MinicCoreBlockId when_true,
                                                          MinicCoreBlockId when_false) {
    MinicCoreTerminator terminator;

    if (context == NULL || context->function == NULL ||
        condition >= context->function->value_count ||
        !minic_type_is_integer(context->function->values[condition].type) ||
        when_true == MINIC_CORE_BLOCK_INVALID || when_false == MINIC_CORE_BLOCK_INVALID) {
        return MINIC_CORE_LOWER_ERROR;
    }
    (void)memset(&terminator, 0, sizeof(terminator));
    terminator.kind = MINIC_CORE_TERMINATOR_CONDITIONAL_BRANCH;
    terminator.span = span;
    terminator.return_value = MINIC_CORE_VALUE_INVALID;
    terminator.conditional.condition = condition;
    terminator.conditional.when_true = when_true;
    terminator.conditional.when_false = when_false;
    return minic_core_function_set_terminator(context->function, context->block_id, &terminator)
               ? MINIC_CORE_LOWER_OK
               : MINIC_CORE_LOWER_ERROR;
}

static MinicCoreLowerStatus lower_switch_case_dispatch(MinicCoreLowerContext *context,
                                                       const MinicStatement *case_statement,
                                                       MinicType selector_type,
                                                       MinicCoreObjectId selector_object,
                                                       MinicCoreBlockId body_target,
                                                       MinicCoreBlockId next_target) {
    const MinicExpression *lower_expression;
    const MinicExpression *upper_expression;
    MinicConstValue lower_constant;
    MinicConstValue lower_converted;
    MinicConstValue upper_constant;
    MinicConstValue upper_converted;
    MinicCoreInstruction instruction;
    MinicCoreValueId bound;
    MinicCoreValueId comparison;
    MinicCoreValueId selector;
    MinicCoreLowerStatus status;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || case_statement == NULL ||
        case_statement->kind != MINIC_STATEMENT_CASE ||
        case_statement->expression == MINIC_EXPRESSION_INVALID ||
        !minic_type_is_integer(selector_type) || body_target == MINIC_CORE_BLOCK_INVALID ||
        next_target == MINIC_CORE_BLOCK_INVALID) {
        return MINIC_CORE_LOWER_ERROR;
    }
    lower_expression =
        minic_c0_program_expression(context->body->program, case_statement->expression);
    if (lower_expression == NULL || !minic_type_is_integer(lower_expression->type) ||
        !minic_const_eval_integer(context->body->program,
                                  context->target,
                                  case_statement->expression,
                                  &lower_constant) ||
        !minic_const_value_convert_integer(context->body->program,
                                           context->target,
                                           &lower_constant,
                                           selector_type,
                                           &lower_converted)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }

    status = minic_core_lower_internal_reload_scalar_value(
        context, case_statement->span, selector_type, selector_object, &selector);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    {
        int64_t lower_value;

        lower_value = 0;
        (void)memcpy(&lower_value, &lower_converted.bits, sizeof(lower_value));
        status = append_switch_integer_constant(context,
                                                lower_expression->span,
                                                selector_type,
                                                lower_value,
                                                &bound);
    }
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    if (case_statement->target_expression == MINIC_EXPRESSION_INVALID) {
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_SCALAR_EQUAL;
        instruction.span = case_statement->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = selector;
        instruction.value.binary.right = bound;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &comparison)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        return set_switch_conditional_branch(
            context, case_statement->span, comparison, body_target, next_target);
    }

    upper_expression =
        minic_c0_program_expression(context->body->program, case_statement->target_expression);
    if (upper_expression == NULL || !minic_type_is_integer(upper_expression->type) ||
        !minic_const_eval_integer(context->body->program,
                                  context->target,
                                  case_statement->target_expression,
                                  &upper_constant) ||
        !minic_const_value_convert_integer(context->body->program,
                                           context->target,
                                           &upper_constant,
                                           selector_type,
                                           &upper_converted)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    {
        MinicCoreBlockId upper_test_block;

        if (!minic_core_function_add_block(context->function, &upper_test_block)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_LESS;
        instruction.span = case_statement->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = selector;
        instruction.value.binary.right = bound;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &comparison)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        status = set_switch_conditional_branch(
            context, case_statement->span, comparison, next_target, upper_test_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }

        context->block_id = upper_test_block;
        {
            int64_t upper_value;

            upper_value = 0;
            (void)memcpy(&upper_value, &upper_converted.bits, sizeof(upper_value));
            status = append_switch_integer_constant(context,
                                                    upper_expression->span,
                                                    selector_type,
                                                    upper_value,
                                                    &bound);
        }
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        status = minic_core_lower_internal_reload_scalar_value(
            context, case_statement->span, selector_type, selector_object, &selector);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        (void)memset(&instruction, 0, sizeof(instruction));
        instruction.kind = MINIC_CORE_INSTRUCTION_INTEGER_LESS;
        instruction.span = case_statement->span;
        instruction.type = minic_type_int();
        instruction.result = MINIC_CORE_VALUE_INVALID;
        instruction.value.binary.left = bound;
        instruction.value.binary.right = selector;
        if (!minic_core_function_append_value_instruction(
                context->function, context->block_id, &instruction, &comparison)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        return set_switch_conditional_branch(
            context, case_statement->span, comparison, next_target, body_target);
    }
}

static bool core_switch_label_has_function_reentry(
    const MinicCoreLowerContext *context, MinicStatementId label_id) {
    const MinicC0Program *program;
    const MinicStatement *label;
    size_t index;

    if (context == NULL || context->body == NULL || context->body->program == NULL) {
        return false;
    }
    program = context->body->program;
    label = minic_c0_program_statement(program, label_id);
    if (label == NULL || label->kind != MINIC_STATEMENT_LABEL) {
        return false;
    }
    if (minic_core_reentry_index_ready(context->reentry_index, program))
        return minic_core_reentry_index_switch_label_referenced(
            context->reentry_index, program, label_id);
    for (index = 0U; index < program->statement_count; ++index) {
        const MinicStatement *source;

        source = minic_c0_program_statement(program, index);
        if (source == NULL) {
            return false;
        }
        if (source->kind == MINIC_STATEMENT_GOTO &&
            source->expression == MINIC_EXPRESSION_INVALID &&
            source->target_statement == label_id) {
            return true;
        }
        if (source->kind == MINIC_STATEMENT_INLINE_ASM &&
            source->inline_asm_id < program->inline_asm_count) {
            const MinicInlineAsm *inline_asm = &program->inline_asms[source->inline_asm_id];
            size_t label_index;

            if (!inline_asm->is_goto) {
                continue;
            }
            for (label_index = 0U; label_index < inline_asm->label_count; ++label_index) {
                if (inline_asm->labels[label_index].target_statement == label_id) {
                    return true;
                }
            }
        }
    }
    for (index = 0U; index < program->expression_count; ++index) {
        const MinicExpression *expression = minic_c0_program_expression(program, index);

        if (expression == NULL) {
            return false;
        }
        if (expression->kind == MINIC_EXPRESSION_LABEL_ADDRESS &&
            expression->value.label_statement_id == label_id) {
            return true;
        }
    }
    return false;
}

/* A break nested under an if/compound block still exits the current switch.
   Breaks nested under a loop or another switch belong to that inner construct.
   This query is intentionally conservative: it is used only to decide whether
   the synthetic switch exit is reachable, never to prune executable source. */
static bool core_switch_block_has_outer_break(const MinicCoreLowerContext *context,
                                              const MinicBlock *block,
                                              bool break_targets_outer) {
    size_t index;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        block == NULL) {
        return true;
    }
    for (index = 0U; index < block->statement_count; ++index) {
        const MinicStatement *statement;

        statement = minic_c0_program_statement(
            context->body->program, block->statements[index]);
        if (statement == NULL) {
            return true;
        }
        if (break_targets_outer && statement->kind == MINIC_STATEMENT_BREAK) {
            return true;
        }
        if (statement->kind == MINIC_STATEMENT_IF) {
            const MinicBlock *then_block;
            const MinicBlock *else_block;

            then_block =
                statement->then_block == MINIC_BLOCK_INVALID
                    ? NULL
                    : minic_c0_program_block(context->body->program, statement->then_block);
            else_block =
                statement->else_block == MINIC_BLOCK_INVALID
                    ? NULL
                    : minic_c0_program_block(context->body->program, statement->else_block);
            if ((then_block != NULL &&
                 core_switch_block_has_outer_break(context, then_block, break_targets_outer)) ||
                (else_block != NULL &&
                 core_switch_block_has_outer_break(context, else_block, break_targets_outer))) {
                return true;
            }
        }
        /* Parser-normalized for/do loops are represented as WHILE. A break
           below either WHILE or SWITCH targets that nested construct, not the
           switch whose case segment we are classifying here. */
    }
    return false;
}


typedef struct MinicCoreNestedSwitchLabel {
    MinicStatementId statement_id;
    const MinicStatement *statement;
    MinicCoreBlockId body_block;
    MinicCoreBlockId test_block;
} MinicCoreNestedSwitchLabel;

/* C case/default labels may appear inside an arbitrary statement nested within
   the selected switch body. They still belong to the nearest enclosing switch;
   labels below a nested switch belong to that inner switch and must not leak
   into the outer dispatch table. Collect the nearest-switch label set in
   source traversal order and remember whether any label is below the root
   block. Parser-normalized blocks can be shared, so the walk is cycle-safe. */
static MinicCoreLowerStatus core_collect_nested_switch_labels(
    MinicCoreLowerContext *context,
    MinicBlockId block_id,
    bool root_block,
    bool *visited_blocks,
    MinicCoreNestedSwitchLabel *labels,
    size_t *label_count,
    bool *has_nested_label) {
    const MinicBlock *block;
    size_t index;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        visited_blocks == NULL || labels == NULL || label_count == NULL ||
        has_nested_label == NULL || block_id == MINIC_BLOCK_INVALID ||
        block_id >= context->body->program->block_count) {
        return MINIC_CORE_LOWER_ERROR;
    }
    if (visited_blocks[block_id]) {
        return MINIC_CORE_LOWER_OK;
    }
    visited_blocks[block_id] = true;
    block = minic_c0_program_block(context->body->program, block_id);
    if (block == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }

    for (index = 0U; index < block->statement_count; ++index) {
        MinicStatementId statement_id;
        const MinicStatement *source_statement;
        MinicCoreLowerStatus status;

        statement_id = block->statements[index];
        source_statement =
            minic_c0_program_statement(context->body->program, statement_id);
        if (source_statement == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (source_statement->kind == MINIC_STATEMENT_CASE ||
            source_statement->kind == MINIC_STATEMENT_DEFAULT) {
            if (*label_count >= MINIC_CORE_SWITCH_LABEL_LIMIT) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            labels[*label_count].statement_id = statement_id;
            labels[*label_count].statement = source_statement;
            labels[*label_count].body_block = MINIC_CORE_BLOCK_INVALID;
            labels[*label_count].test_block = MINIC_CORE_BLOCK_INVALID;
            *label_count += 1U;
            if (!root_block) {
                *has_nested_label = true;
            }
        }

        /* The nearest enclosing switch owns case/default. Never descend into a
           nested switch while building the current switch's dispatch table. */
        if (source_statement->kind == MINIC_STATEMENT_SWITCH) {
            continue;
        }
        if (source_statement->then_block != MINIC_BLOCK_INVALID) {
            status = core_collect_nested_switch_labels(context,
                                                       source_statement->then_block,
                                                       false,
                                                       visited_blocks,
                                                       labels,
                                                       label_count,
                                                       has_nested_label);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
        if (source_statement->else_block != MINIC_BLOCK_INVALID) {
            status = core_collect_nested_switch_labels(context,
                                                       source_statement->else_block,
                                                       false,
                                                       visited_blocks,
                                                       labels,
                                                       label_count,
                                                       has_nested_label);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
    }
    return MINIC_CORE_LOWER_OK;
}

static MinicCoreLowerStatus lower_nested_case_switch(
    MinicCoreLowerContext *context,
    const MinicStatement *statement,
    const MinicBlock *body,
    MinicType selector_type,
    bool *terminated) {
    MinicCoreNestedSwitchLabel labels[MINIC_CORE_SWITCH_LABEL_LIMIT];
    bool *visited_blocks;
    MinicCoreBlockId body_entry;
    MinicCoreBlockId default_target;
    MinicCoreBlockId dispatch_target;
    MinicCoreBlockId exit_block;
    MinicCoreBlockId saved_break_target;
    MinicCoreObjectId selector_object;
    MinicCoreValueId selector_normalized;
    MinicCoreValueId selector_source;
    MinicCoreLowerStatus status;
    size_t case_count;
    size_t default_label;
    size_t first_case_label;
    size_t label_count;
    size_t label_index;
    bool body_terminated;
    bool has_nested_label;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || statement == NULL || body == NULL ||
        terminated == NULL || context->target == NULL) {
        return MINIC_CORE_LOWER_ERROR;
    }

    visited_blocks =
        (bool *)calloc(context->body->program->block_count, sizeof(*visited_blocks));
    if (visited_blocks == NULL && context->body->program->block_count != 0U) {
        return MINIC_CORE_LOWER_ERROR;
    }
    label_count = 0U;
    has_nested_label = false;
    status = core_collect_nested_switch_labels(context,
                                               statement->then_block,
                                               true,
                                               visited_blocks,
                                               labels,
                                               &label_count,
                                               &has_nested_label);
    free(visited_blocks);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (!has_nested_label || label_count == 0U) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }

    case_count = 0U;
    default_label = SIZE_MAX;
    first_case_label = SIZE_MAX;
    for (label_index = 0U; label_index < label_count; ++label_index) {
        const MinicStatement *label_statement = labels[label_index].statement;

        if (!minic_core_loop_cleanup_edge_is_empty(label_statement) ||
            label_statement->then_block != MINIC_BLOCK_INVALID ||
            label_statement->else_block != MINIC_BLOCK_INVALID) {
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        if (label_statement->kind == MINIC_STATEMENT_CASE) {
            if (label_statement->expression == MINIC_EXPRESSION_INVALID) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (first_case_label == SIZE_MAX) {
                first_case_label = label_index;
            }
            case_count += 1U;
        } else {
            if (default_label != SIZE_MAX ||
                label_statement->expression != MINIC_EXPRESSION_INVALID ||
                label_statement->target_expression != MINIC_EXPRESSION_INVALID) {
                return MINIC_CORE_LOWER_ERROR;
            }
            default_label = label_index;
        }
    }

    status = lower_expression(context, statement->expression, &selector_source);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = minic_core_lower_internal_append_integer_conversion(
        context, statement->span, selector_type, selector_source, &selector_normalized);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = minic_core_lower_internal_spill_scalar_value(
        context, statement->span, selector_type, selector_normalized, &selector_object);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    if (!minic_core_function_add_block(context->function, &exit_block) ||
        !minic_core_function_add_block(context->function, &body_entry)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    for (label_index = 0U; label_index < label_count; ++label_index) {
        status = ensure_statement_block(
            context, labels[label_index].statement_id, &labels[label_index].body_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (labels[label_index].statement->kind == MINIC_STATEMENT_CASE &&
            !minic_core_function_add_block(
                context->function, &labels[label_index].test_block)) {
            return MINIC_CORE_LOWER_ERROR;
        }
    }

    default_target =
        default_label == SIZE_MAX ? exit_block : labels[default_label].body_block;
    dispatch_target =
        first_case_label == SIZE_MAX ? default_target : labels[first_case_label].test_block;
    status = minic_core_lower_internal_set_branch(context, context->block_id, statement->span, dispatch_target);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    if (case_count != 0U) {
        for (label_index = 0U; label_index < label_count; ++label_index) {
            size_t next_label;
            MinicCoreBlockId next_target;

            if (labels[label_index].statement->kind != MINIC_STATEMENT_CASE) {
                continue;
            }
            next_target = default_target;
            for (next_label = label_index + 1U; next_label < label_count; ++next_label) {
                if (labels[next_label].statement->kind == MINIC_STATEMENT_CASE) {
                    next_target = labels[next_label].test_block;
                    break;
                }
            }
            context->block_id = labels[label_index].test_block;
            status = lower_switch_case_dispatch(context,
                                                labels[label_index].statement,
                                                selector_type,
                                                selector_object,
                                                labels[label_index].body_block,
                                                next_target);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
    }

    /* Lower the source body exactly once. body_entry is deliberately not
       reachable from the selector dispatch: it owns only source statements
       before the first case. Pre-created case blocks act as re-entry points
       when dispatch jumps into an inner if/loop/compound. */
    context->block_id = body_entry;
    saved_break_target = context->break_target;
    context->break_target = exit_block;
    body_terminated = false;
    status = minic_core_lower_internal_lower_block(context, body, &body_terminated);
    context->break_target = saved_break_target;
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    if (!body_terminated) {
        status = minic_core_lower_internal_set_branch(context, context->block_id, statement->span, exit_block);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
    }

    context->block_id = exit_block;
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
    } else {
        *terminated = false;
    }
    return MINIC_CORE_LOWER_OK;
}

/* M176_SWITCH_POST_BREAK_LABEL_REENTRY: a direct break ends ordinary switch
   fallthrough, but a later ordinary C label remains a valid goto target. Keep
   that re-entry path separate from the case segment so break still reaches the
   synthetic switch exit and only a genuinely referenced label can revive the
   unreachable tail. */
/* Statements before the first case/default are dead on switch entry.
 * Do not discard a nested function-goto/asm-goto label silently: it can
 * still be entered from elsewhere. A depth/AST failure is fail-closed. */
static bool core_switch_dead_prefix_has_reentry(
    const MinicCoreLowerContext *context, MinicStatementId id,
    unsigned int depth) {
    const MinicC0Program *program;
    const MinicStatement *stmt;
    MinicBlockId children[2];
    size_t child;

    if (context == NULL || context->body == NULL ||
        context->body->program == NULL || depth >= 64U)
        return true;
    program = context->body->program;
    stmt = minic_c0_program_statement(program, id);
    if (stmt == NULL) return true;
    if (stmt->kind == MINIC_STATEMENT_LABEL &&
        core_switch_label_has_function_reentry(context, id))
        return true;
    /* The AST only owns statement child blocks for actual control-flow
     * statements. RETURN, BREAK, LABEL and inline ASM have no such children;
     * consulting their otherwise-unused block fields can walk an unrelated
     * function block and falsely detect goto re-entry (particularly after a
     * constant switch case that returns). Keep the global label-reference
     * check above for genuine user LABEL nodes. */
    if (stmt->kind != MINIC_STATEMENT_IF &&
        stmt->kind != MINIC_STATEMENT_WHILE &&
        stmt->kind != MINIC_STATEMENT_SWITCH)
        return false;
    children[0] = stmt->then_block;
    children[1] = stmt->else_block;
    for (child = 0U; child < 2U; ++child) {
        const MinicBlock *block;
        size_t k;
        if (children[child] == MINIC_BLOCK_INVALID) continue;
        block = minic_c0_program_block(program, children[child]);
        if (block == NULL) return true;
        for (k = 0U; k < block->statement_count; ++k)
            if (core_switch_dead_prefix_has_reentry(
                    context, block->statements[k], depth + 1U))
                return true;
    }
    return false;
}

MinicCoreLowerStatus
minic_core_lower_switch(MinicCoreLowerContext *context, const MinicStatement *statement, bool *terminated) {
    const MinicBlock *body;
    const MinicExpression *selector_expression;
    MinicCoreSwitchLabel labels[MINIC_CORE_SWITCH_LABEL_LIMIT];
    MinicCoreBlockId default_target;
    MinicCoreBlockId dispatch_target;
    MinicCoreBlockId exit_block;
    MinicCoreObjectId selector_object;
    MinicCoreValueId selector_normalized;
    MinicCoreValueId selector_source;
    MinicCoreLowerStatus status;
    MinicType selector_type;
    size_t case_count;
    size_t default_label;
    size_t first_case_label;
    size_t label_count;
    size_t first_pre_case_index;
    size_t pre_case_label_count;
    size_t source_index;
    size_t constant_entry_label;
    bool prune_constant_prefix;
    bool prune_constant_suffix;
    bool all_segments_terminate;
    bool segment_breaks[MINIC_CORE_SWITCH_LABEL_LIMIT];
    bool segment_terminates[MINIC_CORE_SWITCH_LABEL_LIMIT];

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        context->function == NULL || statement == NULL || terminated == NULL ||
        statement->kind != MINIC_STATEMENT_SWITCH ||
        !minic_core_loop_cleanup_edge_is_empty(statement) ||
        statement->expression == MINIC_EXPRESSION_INVALID ||
        statement->then_block == MINIC_BLOCK_INVALID ||
        statement->else_block != MINIC_BLOCK_INVALID || context->target == NULL) {
        if (context != NULL && context->source_function != NULL && statement != NULL) {
            (void)fprintf(stderr,
                          "CORE_SWITCH_DETAIL function=%s gate=entry cleanup=%llu stop=%llu "
                          "expr=%u then=%u else=%u\n",
                          context->source_function->name,
                          (unsigned long long)statement->cleanup_context,
                          (unsigned long long)statement->cleanup_stop_context,
                          (unsigned)statement->expression,
                          (unsigned)statement->then_block,
                          (unsigned)statement->else_block);
        }
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
    selector_expression =
        minic_c0_program_expression(context->body->program, statement->expression);
    body = minic_c0_program_block(context->body->program, statement->then_block);
    if (selector_expression == NULL || body == NULL ||
        !minic_type_is_integer(selector_expression->type) ||
        !minic_target_info_integer_promotion_for_program(
            context->target, context->body->program, selector_expression->type, &selector_type)) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }

    {
        MinicCoreNestedSwitchLabel nested_labels[MINIC_CORE_SWITCH_LABEL_LIMIT];
        bool *visited_blocks;
        bool has_nested_label;
        size_t nested_label_count;

        visited_blocks =
            (bool *)calloc(context->body->program->block_count, sizeof(*visited_blocks));
        if (visited_blocks == NULL && context->body->program->block_count != 0U) {
            return MINIC_CORE_LOWER_ERROR;
        }
        nested_label_count = 0U;
        has_nested_label = false;
        status = core_collect_nested_switch_labels(context,
                                                   statement->then_block,
                                                   true,
                                                   visited_blocks,
                                                   nested_labels,
                                                   &nested_label_count,
                                                   &has_nested_label);
        free(visited_blocks);
        if (status != MINIC_CORE_LOWER_OK) {
            return status;
        }
        if (has_nested_label) {
            return lower_nested_case_switch(
                context, statement, body, selector_type, terminated);
        }
    }

    constant_entry_label = SIZE_MAX;
    prune_constant_prefix = false;
    prune_constant_suffix = false;
    case_count = 0U;
    default_label = SIZE_MAX;
    first_case_label = SIZE_MAX;
    label_count = 0U;
    pre_case_label_count = 0U;
    first_pre_case_index = SIZE_MAX;
    for (source_index = 0U; source_index < body->statement_count; ++source_index) {
        const MinicStatement *source_statement;

        source_statement =
            minic_c0_program_statement(context->body->program, body->statements[source_index]);
        if (source_statement == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (source_statement->kind != MINIC_STATEMENT_CASE &&
            source_statement->kind != MINIC_STATEMENT_DEFAULT) {
            if (label_count == 0U) {
                const MinicStatementId prefix_id = body->statements[source_index];
                /* The only entry to this part of a switch is a real
                 * function-level goto/asm-goto. Preserve these labels. */
                if (source_statement->kind == MINIC_STATEMENT_LABEL &&
                    core_switch_label_has_function_reentry(context, prefix_id)) {
                    if (source_statement->target_expression != MINIC_EXPRESSION_INVALID ||
                        source_statement->expression != MINIC_EXPRESSION_INVALID ||
                        source_statement->target_statement != MINIC_STATEMENT_INVALID ||
                        pre_case_label_count >= MINIC_CORE_SWITCH_LABEL_LIMIT)
                        return MINIC_CORE_LOWER_UNSUPPORTED;
                    if (first_pre_case_index == SIZE_MAX)
                        first_pre_case_index = source_index;
                    ++pre_case_label_count;
                    continue;
                }
                /* A bare statement preceding the first case/default is
                 * unreachable from switch dispatch. TinyCC's original
                 * ARM64 prepare_dynamic_rel has assignment/break/if nodes
                 * here after target-conditioned case labels disappear.
                 * Ignore such dead statements, but reject any descendant
                 * with a genuine function-goto reentry path. */
                if (core_switch_dead_prefix_has_reentry(
                        context, prefix_id, 0U))
                    return MINIC_CORE_LOWER_UNSUPPORTED;
                continue;
            }
            continue;
        }
        if (!minic_core_loop_cleanup_edge_is_empty(source_statement) ||
            source_statement->then_block != MINIC_BLOCK_INVALID ||
            source_statement->else_block != MINIC_BLOCK_INVALID ||
            label_count >= MINIC_CORE_SWITCH_LABEL_LIMIT) {
            (void)fprintf(stderr,
                          "CORE_SWITCH_DETAIL function=%s gate=label source_index=%zu kind=%d "
                          "cleanup=%llu stop=%llu then=%u else=%u labels=%zu\n",
                          context->source_function->name,
                          source_index,
                          (int)source_statement->kind,
                          (unsigned long long)source_statement->cleanup_context,
                          (unsigned long long)source_statement->cleanup_stop_context,
                          (unsigned)source_statement->then_block,
                          (unsigned)source_statement->else_block,
                          label_count);
            return MINIC_CORE_LOWER_UNSUPPORTED;
        }
        labels[label_count].source_index = source_index;
        labels[label_count].statement = source_statement;
        labels[label_count].body_block = MINIC_CORE_BLOCK_INVALID;
        labels[label_count].test_block = MINIC_CORE_BLOCK_INVALID;
        if (source_statement->kind == MINIC_STATEMENT_CASE) {
            if (source_statement->expression == MINIC_EXPRESSION_INVALID) {
                return MINIC_CORE_LOWER_ERROR;
            }
            if (first_case_label == SIZE_MAX) {
                first_case_label = label_count;
            }
            case_count += 1U;
        } else {
            if (default_label != SIZE_MAX ||
                source_statement->expression != MINIC_EXPRESSION_INVALID ||
                source_statement->target_expression != MINIC_EXPRESSION_INVALID) {
                return MINIC_CORE_LOWER_ERROR;
            }
            default_label = label_count;
        }
        label_count += 1U;
    }

    status = lower_expression(context, statement->expression, &selector_source);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = minic_core_lower_internal_append_integer_conversion(
        context, selector_expression->span, selector_type, selector_source, &selector_normalized);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }
    status = minic_core_lower_internal_spill_scalar_value(
        context, selector_expression->span, selector_type, selector_normalized, &selector_object);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    if (!minic_core_function_add_block(context->function, &exit_block)) {
        return MINIC_CORE_LOWER_ERROR;
    }
    for (source_index = 0U; source_index < label_count; ++source_index) {
        if (!minic_core_function_add_block(context->function, &labels[source_index].body_block)) {
            return MINIC_CORE_LOWER_ERROR;
        }
        if (labels[source_index].statement->kind == MINIC_STATEMENT_CASE &&
            !minic_core_function_add_block(context->function, &labels[source_index].test_block)) {
            return MINIC_CORE_LOWER_ERROR;
        }
    }

    if (pre_case_label_count != 0U && label_count == 0U)
        return MINIC_CORE_LOWER_UNSUPPORTED;

    default_target = default_label == SIZE_MAX ? exit_block : labels[default_label].body_block;
    dispatch_target =
        first_case_label == SIZE_MAX ? default_target : labels[first_case_label].test_block;

    /* M177_CONSTANT_SWITCH_CFG_OWNER: an integer constant-expression selector
       has exactly one switch-entry edge. Keep case-segment fallthrough intact,
       but bypass the synthetic comparison chain when every case is a simple
       scalar case. Range cases stay on the established generic dispatch path. */
    {
        MinicConstValue selector_constant;
        MinicConstValue selector_converted;

        if (core_const_eval_integer_with_locals(context,
                                                statement->expression,
                                                &selector_constant) &&
            minic_const_value_convert_integer(context->body->program,
                                              context->target,
                                              &selector_constant,
                                              selector_type,
                                              &selector_converted)) {
            size_t constant_target = SIZE_MAX;
            size_t dispatch_index;
            bool simple_cases = true;

            for (dispatch_index = 0U; dispatch_index < label_count; ++dispatch_index) {
                const MinicStatement *case_statement = labels[dispatch_index].statement;
                MinicConstValue case_constant;
                MinicConstValue case_converted;

                if (case_statement->kind != MINIC_STATEMENT_CASE) {
                    continue;
                }
                if (case_statement->target_expression != MINIC_EXPRESSION_INVALID ||
                    !minic_const_eval_integer(context->body->program,
                                              context->target,
                                              case_statement->expression,
                                              &case_constant) ||
                    !minic_const_value_convert_integer(context->body->program,
                                                       context->target,
                                                       &case_constant,
                                                       selector_type,
                                                       &case_converted)) {
                    simple_cases = false;
                    break;
                }
                if (constant_target == SIZE_MAX &&
                    case_converted.bits == selector_converted.bits) {
                    constant_target = dispatch_index;
                }
            }
            if (simple_cases) {
                dispatch_target = constant_target == SIZE_MAX
                                      ? default_target
                                      : labels[constant_target].body_block;
                /* C fallthrough only goes forward; retain selected label
                 * for a safe shared CFG reachability preflight below. */
                constant_entry_label = constant_target;
            }
        }
    }
    status = minic_core_lower_internal_set_branch(context, context->block_id, statement->span, dispatch_target);
    if (status != MINIC_CORE_LOWER_OK) {
        return status;
    }

    /* A constant switch has one real source entry. Prior case segments
     * cannot be reached by ordinary forward fallthrough. Skip their C
     * lowering only if NO named label anywhere in the dead prefix has an
     * external goto/asm-goto/label-address reentry; otherwise keep the
     * original CFG, including every side effect. This is target-neutral. */
    /* An externally entered PRE-CASE named label may fall through into
     * case 0 regardless of constant switch dispatch: retain the entire
     * source prefix when such a reentry exists. */
    if (pre_case_label_count == 0U &&
        constant_entry_label != SIZE_MAX && constant_entry_label != 0U) {
        size_t label_index;
        prune_constant_prefix = true;
        for (label_index = 0U; label_index < constant_entry_label &&
                               prune_constant_prefix; ++label_index) {
            const size_t first = labels[label_index].source_index + 1U;
            const size_t last = labels[label_index + 1U].source_index;
            size_t k;
            for (k = first; k < last; ++k) {
                if (core_switch_dead_prefix_has_reentry(
                        context, body->statements[k], 0U)) {
                    prune_constant_prefix = false;
                    break;
                }
            }
        }
    }

    if (case_count != 0U) {
        size_t label_index;

        for (label_index = 0U; label_index < label_count; ++label_index) {
            size_t next_label;
            MinicCoreBlockId next_target;

            if (labels[label_index].statement->kind != MINIC_STATEMENT_CASE) {
                continue;
            }
            next_target = default_target;
            for (next_label = label_index + 1U; next_label < label_count; ++next_label) {
                if (labels[next_label].statement->kind == MINIC_STATEMENT_CASE) {
                    next_target = labels[next_label].test_block;
                    break;
                }
            }
            context->block_id = labels[label_index].test_block;
            status = lower_switch_case_dispatch(context,
                                                labels[label_index].statement,
                                                selector_type,
                                                selector_object,
                                                labels[label_index].body_block,
                                                next_target);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
    }

    /* A function-level goto may enter a label before the first case.
     * The ordinary switch dispatch never enters this prefix, but the goto
     * must execute every subsequent source statement, including a break,
     * rather than teleporting directly to the first case body. Lower the
     * reachable suffix in a detached CFG block. Restore local facts because
     * the switch dispatch and case segments do not execute that suffix. */
    if (pre_case_label_count != 0U) {
        MinicCoreLocalIntegerConstant *saved_facts;
        MinicCoreBlockId saved_block;
        MinicCoreBlockId saved_break_target;
        MinicCoreBlockId prefix_entry;
        MinicBlock prefix_segment;
        bool prefix_terminated;

        if (first_pre_case_index == SIZE_MAX ||
            first_pre_case_index >= labels[0].source_index)
            return MINIC_CORE_LOWER_ERROR;
        saved_facts = minic_core_local_facts_snapshot(context);
        if (context->source_function != NULL &&
            context->source_function->local_count != 0U &&
            saved_facts == NULL)
            return MINIC_CORE_LOWER_ERROR;
        if (!minic_core_function_add_block(context->function, &prefix_entry)) {
            free(saved_facts);
            return MINIC_CORE_LOWER_ERROR;
        }
        prefix_segment = *body;
        prefix_segment.statements = body->statements + first_pre_case_index;
        prefix_segment.statement_count =
            labels[0].source_index - first_pre_case_index;
        prefix_segment.statement_capacity = prefix_segment.statement_count;
        saved_block = context->block_id;
        saved_break_target = context->break_target;
        context->block_id = prefix_entry;
        context->break_target = exit_block;
        prefix_terminated = false;
        status = minic_core_lower_internal_lower_block(
            context, &prefix_segment, &prefix_terminated);
        context->break_target = saved_break_target;
        if (status == MINIC_CORE_LOWER_OK && !prefix_terminated)
            status = minic_core_lower_internal_set_branch(
                context, context->block_id, statement->span,
                labels[0].body_block);
        context->block_id = saved_block;
        minic_core_local_facts_restore(context, saved_facts);
        free(saved_facts);
        if (status != MINIC_CORE_LOWER_OK) return status;
    }

    /* M101_TERMINATING_SWITCH_FALLTHROUGH: termination is a property of the
       path starting at a label, not only of that label's immediate segment.
       Record each segment first, then prove fallthrough chains backwards. A
       break still reaches the synthetic exit and is therefore non-terminating. */
    (void)memset(segment_breaks, 0, sizeof(segment_breaks));
    (void)memset(segment_terminates, 0, sizeof(segment_terminates));
    for (source_index = 0U; source_index < label_count; ++source_index) {
        MinicBlock segment;
        MinicCoreBlockId fallthrough_target;
        size_t break_index;
        size_t reentry_index;
        size_t segment_begin;
        size_t segment_end;
        size_t scan;
        MinicCoreBlockId saved_break_target;
        bool segment_terminated;

        if ((prune_constant_prefix && source_index < constant_entry_label) ||
            (prune_constant_suffix && source_index > constant_entry_label)) {
            MinicCoreTerminator unreachable;
            (void)memset(&unreachable, 0, sizeof(unreachable));
            unreachable.kind = MINIC_CORE_TERMINATOR_UNREACHABLE;
            unreachable.span = labels[source_index].statement->span;
            unreachable.return_value = MINIC_CORE_VALUE_INVALID;
            unreachable.return_object = MINIC_CORE_OBJECT_INVALID;
            /* Constant source entry bypasses these C segments, but each
             * block still has a valid Core terminator. No machine code
             * may represent dead GNU assembly or accidental effects. */
            if (!minic_core_function_set_terminator(
                    context->function, labels[source_index].body_block,
                    &unreachable)) return MINIC_CORE_LOWER_ERROR;
            segment_terminates[source_index] = true;
            segment_breaks[source_index] = false;
            continue;
        }

        segment_begin = labels[source_index].source_index + 1U;
        segment_end = source_index + 1U < label_count ? labels[source_index + 1U].source_index
                                                      : body->statement_count;
        break_index = SIZE_MAX;
        reentry_index = SIZE_MAX;
        for (scan = segment_begin; scan < segment_end; ++scan) {
            const MinicStatement *segment_statement;

            segment_statement =
                minic_c0_program_statement(context->body->program, body->statements[scan]);
            if (segment_statement == NULL) {
                return MINIC_CORE_LOWER_ERROR;
            }
            /* A direct break terminates ordinary switch fallthrough. A later
               ordinary LABEL is reachable only when the function really owns
               a goto/asm-goto/label-address edge to it; split that tail into
               its own Core entry instead of reconnecting the break path. */
            if (break_index != SIZE_MAX &&
                segment_statement->kind != MINIC_STATEMENT_BREAK) {
                if (segment_statement->kind == MINIC_STATEMENT_LABEL &&
                    core_switch_label_has_function_reentry(
                        context, body->statements[scan])) {
                    reentry_index = scan;
                    break;
                }
                /* The tail after an unconditional switch break is unreachable
                   from this case segment. Keep scanning only to discover a real
                   function-level label re-entry; ordinary statements need no Core
                   lowering because no source path can execute them. */
                continue;
            }
            if (segment_statement->kind == MINIC_STATEMENT_BREAK) {
                if (!minic_core_loop_cleanup_edge_is_empty(segment_statement)) {
                    (void)fprintf(stderr,
                                  "CORE_SWITCH_DETAIL function=%s gate=break-cleanup "
                                  "source_index=%zu scan=%zu cleanup=%llu stop=%llu\n",
                                  context->source_function->name,
                                  source_index,
                                  scan,
                                  (unsigned long long)segment_statement->cleanup_context,
                                  (unsigned long long)segment_statement->cleanup_stop_context);
                    return MINIC_CORE_LOWER_UNSUPPORTED;
                }
                if (break_index == SIZE_MAX) {
                    break_index = scan;
                }
            }
        }

        context->block_id = labels[source_index].body_block;
        segment_terminated = false;
        segment = *body;
        segment.statements = body->statements + segment_begin;
        segment.statement_count =
            (break_index == SIZE_MAX ? segment_end : break_index) - segment_begin;
        segment.statement_capacity = segment.statement_count;
        if (segment.statement_count != 0U) {
            saved_break_target = context->break_target;
            context->break_target = exit_block;
            status = minic_core_lower_internal_lower_block(context, &segment, &segment_terminated);
            context->break_target = saved_break_target;
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }
        segment_terminates[source_index] = segment_terminated;
        segment_breaks[source_index] =
            break_index != SIZE_MAX ||
            core_switch_block_has_outer_break(context, &segment, true);

        /* Constant entry + proven direct source break/return ends EVERY
         * ordinary C fallthrough edge to later case bodies. Those later
         * cases are semantically unreachable even when they contain
         * target-specific ISA templates (Linux __smp_store_release has
         * three such cases for a byte-sized spinlock). A nested conditional
         * break is NOT sufficient: only the explicit top-level break
         * recorded by this case walker or a terminated source segment is.
         * Function-level goto/asm-goto/label-address into any later body
         * defeats the entire suffix prune. Keep all ISA knowledge in Target. */
        if (constant_entry_label != SIZE_MAX &&
            pre_case_label_count == 0U &&
            source_index >= constant_entry_label &&
            !prune_constant_suffix &&
            reentry_index == SIZE_MAX &&
            (break_index != SIZE_MAX || segment_terminated)) {
            size_t later;
            bool safe = true;
            for (later = source_index + 1U; later < label_count && safe; ++later) {
                const size_t first = labels[later].source_index + 1U;
                const size_t limit = later + 1U < label_count
                    ? labels[later + 1U].source_index
                    : body->statement_count;
                size_t k;
                for (k = first; k < limit; ++k) {
                    if (core_switch_dead_prefix_has_reentry(
                            context, body->statements[k], 0U)) {
                        safe = false;
                        break;
                    }
                }
            }
            prune_constant_suffix = safe;
        }
        /* A segment may end in a return/goto on every linear path, yet a
         * separate C label after an unconditional break is entered by an
         * earlier function-level goto. Its detached entry MUST still be
         * lowered even when the preceding case body terminated. In
         * particular Linux retbleed has `goto do_cmd_auto;` followed by
         * `break; do_cmd_auto: case AUTO:`. Skipping the reentry would leave
         * a live branch into an empty, unterminated Core block. */
        if (!segment_terminated) {
            if (break_index != SIZE_MAX) {
                fallthrough_target = exit_block;
            } else if (source_index + 1U < label_count) {
                fallthrough_target = labels[source_index + 1U].body_block;
            } else {
                fallthrough_target = exit_block;
            }
            status = minic_core_lower_internal_set_branch(
                context, context->block_id, statement->span, fallthrough_target);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
        }

        if (reentry_index != SIZE_MAX) {
            const MinicStatement *reentry_label;
            MinicCoreBlockId reentry_block;
            MinicBlock reentry_segment;
            bool reentry_terminated;

            reentry_label = minic_c0_program_statement(
                context->body->program, body->statements[reentry_index]);
            if (reentry_label == NULL || reentry_label->kind != MINIC_STATEMENT_LABEL ||
                reentry_label->target_expression != MINIC_EXPRESSION_INVALID ||
                reentry_label->expression != MINIC_EXPRESSION_INVALID ||
                reentry_label->target_statement != MINIC_STATEMENT_INVALID) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
            status = ensure_statement_block(
                context, body->statements[reentry_index], &reentry_block);
            if (status != MINIC_CORE_LOWER_OK) {
                return status;
            }
            context->block_id = reentry_block;
            reentry_terminated = false;
            reentry_segment = *body;
            reentry_segment.statements = body->statements + reentry_index + 1U;
            reentry_segment.statement_count = segment_end - (reentry_index + 1U);
            reentry_segment.statement_capacity = reentry_segment.statement_count;
            if (reentry_segment.statement_count != 0U) {
                saved_break_target = context->break_target;
                context->break_target = exit_block;
                status = minic_core_lower_internal_lower_block(context, &reentry_segment, &reentry_terminated);
                context->break_target = saved_break_target;
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
            if (!reentry_terminated) {
                MinicCoreBlockId reentry_fallthrough;

                reentry_fallthrough =
                    source_index + 1U < label_count
                        ? labels[source_index + 1U].body_block
                        : exit_block;
                status = minic_core_lower_internal_set_branch(
                    context, context->block_id, reentry_label->span, reentry_fallthrough);
                if (status != MINIC_CORE_LOWER_OK) {
                    return status;
                }
            }
        }
    }

    all_segments_terminate = default_label != SIZE_MAX && label_count != 0U;
    if (all_segments_terminate) {
        bool next_path_terminates = false;

        for (source_index = label_count; source_index-- > 0U;) {
            bool path_terminates;

            /* A segment can have a terminating continuation while another
               branch breaks to the synthetic switch exit (for example
               `if (bad) break; return value;`). Such a break makes the switch
               itself non-terminating regardless of the segment continuation. */
            path_terminates =
                !segment_breaks[source_index] &&
                (segment_terminates[source_index] ||
                 (source_index + 1U < label_count && next_path_terminates));
            if (!path_terminates) {
                all_segments_terminate = false;
            }
            next_path_terminates = path_terminates;
        }
    }

    context->block_id = exit_block;
    if (all_segments_terminate) {
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

