#!/usr/bin/env python3
from pathlib import Path

path = Path("src/core/core_lower.c")
text = path.read_text()

marker = '''static MinicCoreLowerStatus
lower_if(MinicCoreLowerContext *context, const MinicStatement *statement, bool *terminated) {'''
helper = r'''static bool core_block_contains_reentry_label_impl(const MinicCoreLowerContext *context,
                                                   const MinicBlock *block,
                                                   bool *visited_blocks) {
    size_t index;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        block == NULL || visited_blocks == NULL) {
        return true;
    }
    for (index = 0U; index < block->statement_count; ++index) {
        const MinicStatement *nested;

        nested = minic_c0_program_statement(
            context->body->program, block->statements[index]);
        if (nested == NULL) {
            return true;
        }
        if (nested->kind == MINIC_STATEMENT_LABEL ||
            nested->kind == MINIC_STATEMENT_CASE ||
            nested->kind == MINIC_STATEMENT_DEFAULT) {
            return true;
        }
        if (nested->then_block != MINIC_BLOCK_INVALID) {
            const MinicBlock *child;

            if (nested->then_block >= context->body->program->block_count) {
                return true;
            }
            if (!visited_blocks[nested->then_block]) {
                visited_blocks[nested->then_block] = true;
                child = minic_c0_program_block(
                    context->body->program, nested->then_block);
                if (child == NULL ||
                    core_block_contains_reentry_label_impl(context, child, visited_blocks)) {
                    return true;
                }
            }
        }
        if (nested->else_block != MINIC_BLOCK_INVALID) {
            const MinicBlock *child;

            if (nested->else_block >= context->body->program->block_count) {
                return true;
            }
            if (!visited_blocks[nested->else_block]) {
                visited_blocks[nested->else_block] = true;
                child = minic_c0_program_block(
                    context->body->program, nested->else_block);
                if (child == NULL ||
                    core_block_contains_reentry_label_impl(context, child, visited_blocks)) {
                    return true;
                }
            }
        }
    }
    return false;
}

static bool core_block_contains_reentry_label(const MinicCoreLowerContext *context,
                                              const MinicBlock *block) {
    bool *visited_blocks;
    bool has_reentry;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        block == NULL || context->body->program->block_count == 0U) {
        return block != NULL;
    }
    visited_blocks =
        (bool *)calloc(context->body->program->block_count, sizeof(*visited_blocks));
    if (visited_blocks == NULL) {
        return true;
    }
    has_reentry = core_block_contains_reentry_label_impl(context, block, visited_blocks);
    free(visited_blocks);
    return has_reentry;
}

static MinicCoreLowerStatus
lower_if(MinicCoreLowerContext *context, const MinicStatement *statement, bool *terminated) {'''
if text.count(marker) != 1:
    raise SystemExit(f"lower_if marker count is {text.count(marker)}, expected 1")
text = text.replace(marker, helper, 1)

anchor = '''    else_source = NULL;
    if (statement->else_block != MINIC_BLOCK_INVALID) {
        else_source = minic_c0_program_block(context->body->program, statement->else_block);
        if (else_source == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
    }

    condition_block = context->block_id;'''
replacement = r'''    else_source = NULL;
    if (statement->else_block != MINIC_BLOCK_INVALID) {
        else_source = minic_c0_program_block(context->body->program, statement->else_block);
        if (else_source == NULL) {
            return MINIC_CORE_LOWER_ERROR;
        }
    }

    /* BusyBox and ordinary GNU C intentionally leave impossible references in
       branches guarded by target constants such as sizeof comparisons and
       ENABLE_* macros.  GCC removes those branches before emission.  Preserve
       the same language-level reachability fact in Core: when the existing
       target-aware integer const-evaluator proves an if condition, lower only
       the selected source block.  A discarded subtree that owns any C/case/
       default label stays on the general CFG path because an enclosing goto or
       switch may legally enter it. */
    if (statement->cleanup_context == statement->cleanup_stop_context &&
        minic_type_is_integer(condition_expression->type)) {
        MinicConstValue condition_value;
        bool condition_is_zero;

        if (minic_const_eval_integer(context->body->program,
                                     context->target,
                                     statement->expression,
                                     &condition_value) &&
            minic_const_value_is_zero(context->body->program,
                                      context->target,
                                      &condition_value,
                                      &condition_is_zero)) {
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
                return lower_block(context, selected_source, terminated);
            }
        }
    }

    condition_block = context->block_id;'''
if text.count(anchor) != 1:
    raise SystemExit(f"lower_if body anchor count is {text.count(anchor)}, expected 1")
text = text.replace(anchor, replacement, 1)
path.write_text(text)
