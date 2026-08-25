#!/usr/bin/env python3
from pathlib import Path

core_path = Path('src/core/core_lower.c')
core = core_path.read_text()
marker = 'M134_UNREACHABLE_TAIL_OWNER'
if marker in core:
    print('M134 unreachable-tail owner already staged')
    raise SystemExit(0)

needle = '''static MinicCoreLowerStatus
lower_block(MinicCoreLowerContext *context, const MinicBlock *source_block, bool *terminated) {
    size_t statement_index;
'''
helper = '''/* M134_UNREACHABLE_TAIL_OWNER: once a structured path has a Core
   terminator, ordinary following statements have no runtime semantics and must
   not make strict lowering fail merely because their expression/control-flow
   owner is unsupported. A structured subtree may be pruned only when no jump
   originates outside that subtree and targets a label inside it. This matters
   because parser-normalized loops contain their own internal goto/label edges;
   those edges disappear together with the unreachable subtree and are not
   function-scope re-entry. Direct goto and asm-goto from outside remain
   fail-closed. Normalized blocks may form a graph, so membership discovery is
   cycle-safe. */
static bool core_mark_block_statement_membership(
    const MinicCoreLowerContext *context,
    MinicBlockId block_id,
    bool *visited_blocks,
    size_t block_count,
    bool *statement_membership,
    size_t statement_count) {
    const MinicBlock *block;
    size_t block_statement_index;

    if (block_id == MINIC_BLOCK_INVALID) {
        return true;
    }
    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        visited_blocks == NULL || statement_membership == NULL || block_id >= block_count) {
        return false;
    }
    if (visited_blocks[block_id]) {
        return true;
    }
    visited_blocks[block_id] = true;
    block = minic_c0_program_block(context->body->program, block_id);
    if (block == NULL) {
        return false;
    }
    for (block_statement_index = 0U;
         block_statement_index < block->statement_count;
         ++block_statement_index) {
        MinicStatementId statement_id;
        const MinicStatement *statement;

        statement_id = block->statements[block_statement_index];
        if (statement_id >= statement_count) {
            return false;
        }
        statement_membership[statement_id] = true;
        statement = minic_c0_program_statement(context->body->program, statement_id);
        if (statement == NULL ||
            !core_mark_block_statement_membership(context,
                                                  statement->then_block,
                                                  visited_blocks,
                                                  block_count,
                                                  statement_membership,
                                                  statement_count) ||
            !core_mark_block_statement_membership(context,
                                                  statement->else_block,
                                                  visited_blocks,
                                                  block_count,
                                                  statement_membership,
                                                  statement_count)) {
            return false;
        }
    }
    return true;
}

static bool core_unreachable_statement_has_external_reentry(
    const MinicCoreLowerContext *context, const MinicStatement *root_statement) {
    const MinicC0Program *program;
    bool *visited_blocks;
    bool *statement_membership;
    bool root_found;
    bool unsafe;
    size_t source_index;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        root_statement == NULL) {
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

    root_found = false;
    for (source_index = 0U; source_index < program->statement_count; ++source_index) {
        const MinicStatement *candidate;

        candidate = minic_c0_program_statement(program, source_index);
        if (candidate == NULL) {
            free(visited_blocks);
            free(statement_membership);
            return true;
        }
        if (candidate == root_statement) {
            statement_membership[source_index] = true;
            root_found = true;
        }
    }
    if (!root_found ||
        !core_mark_block_statement_membership(context,
                                              root_statement->then_block,
                                              visited_blocks,
                                              program->block_count,
                                              statement_membership,
                                              program->statement_count) ||
        !core_mark_block_statement_membership(context,
                                              root_statement->else_block,
                                              visited_blocks,
                                              program->block_count,
                                              statement_membership,
                                              program->statement_count)) {
        free(visited_blocks);
        free(statement_membership);
        return true;
    }

    unsafe = false;
    for (source_index = 0U; source_index < program->statement_count && !unsafe;
         ++source_index) {
        const MinicStatement *source;

        if (statement_membership[source_index]) {
            continue;
        }
        source = minic_c0_program_statement(program, source_index);
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
            const MinicInlineAsm *inline_asm;
            size_t label_index;

            inline_asm = &program->inline_asms[source->inline_asm_id];
            if (!inline_asm->is_goto) {
                continue;
            }
            for (label_index = 0U; label_index < inline_asm->label_count; ++label_index) {
                MinicStatementId target;

                target = inline_asm->labels[label_index].target_statement;
                if (target < program->statement_count && statement_membership[target]) {
                    unsafe = true;
                    break;
                }
            }
        }
    }

    free(visited_blocks);
    free(statement_membership);
    return unsafe;
}

static MinicCoreLowerStatus
lower_block(MinicCoreLowerContext *context, const MinicBlock *source_block, bool *terminated) {
    size_t statement_index;
'''
if core.count(needle) != 1:
    raise SystemExit(f'expected one lower_block definition, found {core.count(needle)}')
core = core.replace(needle, helper, 1)

old_guard = '''        if (block_terminated) {
            /* Parser scope exit materializes the same cleanup expression for
               ordinary fallthrough. A return edge above has already consumed
               that cleanup, so the unreachable duplicate must not run again.
               Keep all other unreachable expression statements fail-closed. */
            if (statement->kind == MINIC_STATEMENT_RETURN ||
                core_is_materialized_cleanup_statement(context, statement)) {
                continue;
            }
            if (statement->kind != MINIC_STATEMENT_LABEL) {
                return MINIC_CORE_LOWER_UNSUPPORTED;
            }
        }
'''
new_guard = '''        if (block_terminated) {
            /* Parser scope exit may materialize cleanup/return tails after an
               already-terminating edge. More generally, non-label statements
               on this path are unreachable and need no Core instructions.
               Preserve fail-closed behavior only for an external goto/asm-goto
               re-entry into that otherwise unreachable structured subtree. */
            if (statement->kind == MINIC_STATEMENT_RETURN ||
                core_is_materialized_cleanup_statement(context, statement)) {
                continue;
            }
            if (statement->kind != MINIC_STATEMENT_LABEL) {
                if (core_unreachable_statement_has_external_reentry(
                        context, statement)) {
                    return MINIC_CORE_LOWER_UNSUPPORTED;
                }
                continue;
            }
        }
'''
if core.count(old_guard) != 1:
    raise SystemExit(f'expected one old unreachable-tail guard, found {core.count(old_guard)}')
core = core.replace(old_guard, new_guard, 1)
core_path.write_text(core)

positive = Path('tests/compiler/c0/m134_unreachable_tail.c')
positive.write_text(r'''int unreachable_expression(int x) {
    return x + 1;
    x = x + 2;
}

int unreachable_if(int x) {
    return x;
    if (x) {
        return 9;
    }
}

int unreachable_while(int x) {
    return x;
    while (x) {
        x = x - 1;
    }
}

int top_level_label_reentry(int x) {
    if (x) {
        goto live;
    }
    return 3;
live:
    return 4;
}
''')

negative = Path('tests/compiler/c0/m134_unreachable_nested_label.c')
negative.write_text(r'''int nested_label_must_not_be_dropped(void) {
    goto inside;
    return 1;
    if (1) {
inside:
        return 2;
    }
}
''')

print('M134 unreachable-tail owner and regressions staged')
