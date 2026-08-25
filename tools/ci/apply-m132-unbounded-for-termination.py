#!/usr/bin/env python3
from pathlib import Path

path = Path('src/core/core_lower.c')
text = path.read_text()
marker = 'M132_UNBOUNDED_FOR_TERMINATION'
if marker in text:
    print('M132 unbounded-for termination owner already staged')
    raise SystemExit(0)

helper_anchor = '''static MinicCoreLowerStatus
lower_while(MinicCoreLowerContext *context,
'''
if helper_anchor not in text:
    raise SystemExit('lower_while anchor changed')

helper = '''/* M132_UNBOUNDED_FOR_TERMINATION: structured control-flow reachability is
   the owner of whether a synthetic loop exit can fall through. In particular,
   an omitted-condition GNU/C `for (;;)` has no natural edge to its exit block;
   only an explicit break may make that exit reachable. */
static bool core_block_has_predecessor(const MinicCoreFunction *function,
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

'''
text = text.replace(helper_anchor, helper + helper_anchor, 1)

end_needle = '''    context->block_id = exit_block;
    *terminated = false;
    return MINIC_CORE_LOWER_OK;
}

static bool core_inline_asm_constraint_is'''
if end_needle not in text:
    raise SystemExit('lower_while exit seam changed')

end_replacement = '''    context->block_id = exit_block;
    if (statement->expression == MINIC_EXPRESSION_INVALID && normalized_for &&
        !core_block_has_predecessor(context->function, exit_block)) {
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

static bool core_inline_asm_constraint_is'''
text = text.replace(end_needle, end_replacement, 1)
path.write_text(text)

Path('tests/compiler/c0/m132_unbounded_for_termination.c').write_text(r'''static int terminal_loop(int value) {
    for (;;) {
        if (value)
            return value;
    }
}

static int breakable_loop(int value) {
    for (;;) {
        if (value)
            break;
        value = 1;
    }
    return value;
}

int main(void) {
    return terminal_loop(1) == 1 && breakable_loop(0) == 1 ? 0 : 1;
}
''')

print('M132 unbounded-for CFG termination owner and regression staged')
