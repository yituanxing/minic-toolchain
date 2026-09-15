#!/usr/bin/env python3
from pathlib import Path

# Constant-if pruning must preserve labels that can actually be entered from
# outside the discarded subtree.  An ordinary label referenced only by a goto
# inside that same discarded subtree is not an external re-entry edge and can
# disappear together with the subtree.  case/default labels and address-taken
# labels remain conservative barriers.
p = Path("src/core/core_lower.c")
text = p.read_text()
marker = "M187_CONSTANT_IF_EXTERNAL_REENTRY"
if marker in text:
    print("MINIC_CONSTANT_IF_EXTERNAL_REENTRY_V0=ALREADY")
    raise SystemExit(0)

start = text.find("static bool core_block_contains_reentry_label_impl(")
end_marker = "\nstatic MinicCoreLowerStatus\nlower_if("
end = text.find(end_marker, start)
if start < 0 or end < 0 or end <= start:
    raise SystemExit("constant-if reentry helper region missing")

replacement = r'''/* M187_CONSTANT_IF_EXTERNAL_REENTRY: mark every statement owned by a
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

    free(visited_blocks);
    free(statement_membership);
    return unsafe;
}
'''

text = text[:start] + replacement + text[end:]
p.write_text(text)
print("MINIC_CONSTANT_IF_EXTERNAL_REENTRY_V0=APPLIED internal_goto=discardable")
