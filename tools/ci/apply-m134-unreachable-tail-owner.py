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
   owner is unsupported. A nested ordinary label is different: C labels have
   function scope and a goto may re-enter that otherwise unreachable subtree.
   Keep such subtrees fail-closed until Core owns jump-into-nested-block CFG. */
static bool core_block_contains_label(const MinicCoreLowerContext *context,
                                      MinicBlockId block_id) {
    const MinicBlock *block;
    size_t statement_index;

    if (block_id == MINIC_BLOCK_INVALID) {
        return false;
    }
    if (context == NULL || context->body == NULL || context->body->program == NULL) {
        return true;
    }
    block = minic_c0_program_block(context->body->program, block_id);
    if (block == NULL) {
        return true;
    }
    for (statement_index = 0U; statement_index < block->statement_count; ++statement_index) {
        const MinicStatement *statement;

        statement = minic_c0_program_statement(
            context->body->program, block->statements[statement_index]);
        if (statement == NULL) {
            return true;
        }
        if (statement->kind == MINIC_STATEMENT_LABEL ||
            core_block_contains_label(context, statement->then_block) ||
            core_block_contains_label(context, statement->else_block)) {
            return true;
        }
    }
    return false;
}

static bool core_unreachable_statement_contains_label(
    const MinicCoreLowerContext *context, const MinicStatement *statement) {
    if (statement == NULL) {
        return true;
    }
    return core_block_contains_label(context, statement->then_block) ||
           core_block_contains_label(context, statement->else_block);
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
               Do not skip a structured subtree containing an ordinary label:
               a function-scope goto may still make that subtree reachable. */
            if (statement->kind == MINIC_STATEMENT_RETURN ||
                core_is_materialized_cleanup_statement(context, statement)) {
                continue;
            }
            if (statement->kind != MINIC_STATEMENT_LABEL) {
                if (core_unreachable_statement_contains_label(context, statement)) {
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
negative.write_text(r'''int nested_label_must_not_be_dropped(int x) {
    return 1;
    if (x) {
inside:
        return 2;
    }
}
''')

print('M134 unreachable-tail owner and regressions staged')
