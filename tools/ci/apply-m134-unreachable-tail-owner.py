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
   owner is unsupported. The exception is a subtree containing a label that is
   an actual function-scope jump target: a direct goto or asm-goto may re-enter
   that otherwise unreachable subtree. Parser-internal loop labels are not jump
   targets and therefore do not block pruning. Normalized blocks may form a
   graph, so discovery is cycle-safe rather than assuming an AST tree. */
static bool core_label_is_jump_target(const MinicCoreLowerContext *context,
                                      MinicStatementId label_id) {
    const MinicC0Program *program;
    size_t statement_index;

    if (context == NULL || context->body == NULL || context->body->program == NULL) {
        return true;
    }
    program = context->body->program;
    for (statement_index = 0U; statement_index < program->statement_count; ++statement_index) {
        const MinicStatement *statement;

        statement = minic_c0_program_statement(program, statement_index);
        if (statement == NULL) {
            return true;
        }
        if (statement->kind == MINIC_STATEMENT_GOTO &&
            statement->target_statement == label_id) {
            return true;
        }
        if (statement->kind == MINIC_STATEMENT_INLINE_ASM &&
            statement->inline_asm_id < program->inline_asm_count) {
            const MinicInlineAsm *inline_asm;
            size_t label_index;

            inline_asm = &program->inline_asms[statement->inline_asm_id];
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
    return false;
}

static bool core_block_contains_jump_target_label_visit(
    const MinicCoreLowerContext *context,
    MinicBlockId block_id,
    bool *visited,
    size_t visited_count) {
    const MinicBlock *block;
    size_t statement_index;

    if (block_id == MINIC_BLOCK_INVALID) {
        return false;
    }
    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        visited == NULL || block_id >= visited_count) {
        return true;
    }
    if (visited[block_id]) {
        return false;
    }
    visited[block_id] = true;
    block = minic_c0_program_block(context->body->program, block_id);
    if (block == NULL) {
        return true;
    }
    for (statement_index = 0U; statement_index < block->statement_count; ++statement_index) {
        MinicStatementId statement_id;
        const MinicStatement *statement;

        statement_id = block->statements[statement_index];
        statement = minic_c0_program_statement(context->body->program, statement_id);
        if (statement == NULL) {
            return true;
        }
        if ((statement->kind == MINIC_STATEMENT_LABEL &&
             core_label_is_jump_target(context, statement_id)) ||
            core_block_contains_jump_target_label_visit(
                context, statement->then_block, visited, visited_count) ||
            core_block_contains_jump_target_label_visit(
                context, statement->else_block, visited, visited_count)) {
            return true;
        }
    }
    return false;
}

static bool core_unreachable_statement_contains_jump_target_label(
    const MinicCoreLowerContext *context, const MinicStatement *statement) {
    const MinicC0Program *program;
    bool *visited;
    bool contains_label;

    if (context == NULL || context->body == NULL || context->body->program == NULL ||
        statement == NULL) {
        return true;
    }
    program = context->body->program;
    if (program->block_count == 0U) {
        return false;
    }
    if (program->block_count > SIZE_MAX / sizeof(*visited)) {
        return true;
    }
    visited = (bool *)calloc(program->block_count, sizeof(*visited));
    if (visited == NULL) {
        return true;
    }
    contains_label =
        core_block_contains_jump_target_label_visit(
            context, statement->then_block, visited, program->block_count) ||
        core_block_contains_jump_target_label_visit(
            context, statement->else_block, visited, program->block_count);
    free(visited);
    return contains_label;
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
               Preserve fail-closed behavior only when a real goto/asm-goto can
               re-enter a nested label in that structured subtree. */
            if (statement->kind == MINIC_STATEMENT_RETURN ||
                core_is_materialized_cleanup_statement(context, statement)) {
                continue;
            }
            if (statement->kind != MINIC_STATEMENT_LABEL) {
                if (core_unreachable_statement_contains_jump_target_label(
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
