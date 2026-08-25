#!/usr/bin/env python3
from pathlib import Path

core_path = Path('src/core/core_lower.c')
core = core_path.read_text()
marker = 'M147_NORMALIZED_FOR_CONTINUE_BINDING_OWNER'
if marker in core:
    print('M147 normalized-for continue binding already staged')
    raise SystemExit(0)
for required in ('M137_DETACHED_LOOP_CONTINUE_OWNER',
                 'M141_SCOPED_LOOP_CONTINUE_TAIL_OWNER'):
    if required not in core:
        raise SystemExit(f'M147 requires staged {required}')

old_early = '''    if (continue_label_statement != MINIC_STATEMENT_INVALID) {
        if (context->statement_blocks == NULL ||
            continue_label_statement >= context->statement_block_count ||
            context->statement_blocks[continue_label_statement] != MINIC_CORE_BLOCK_INVALID) {
            return MINIC_CORE_LOWER_ERROR;
        }
        /* CORE_LOOP_CONTINUE_TARGET_V1: a parser-owned while continue label
           denotes condition re-evaluation. Bind the source label directly to
           the real condition block before lowering the body, so continue does
           not manufacture an orphan block. */
        context->statement_blocks[continue_label_statement] = condition_block;
    }
    status = set_branch(context, preheader_block, statement->span, condition_block);
'''
new_early = '''    status = set_branch(context, preheader_block, statement->span, condition_block);
'''
if core.count(old_early) != 1:
    raise SystemExit(f'M147 expected one early continue binding seam, found {core.count(old_early)}')
core = core.replace(old_early, new_early, 1)

needle = '''    if (statement->expression == MINIC_EXPRESSION_INVALID && !normalized_for) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
'''
replacement = '''    /* M147_NORMALIZED_FOR_CONTINUE_BINDING_OWNER: the same-span detached-label
       heuristic is parser-normalization metadata, not a property of an ordinary
       source while.  M145/M146 demonstrated that pre-binding such a label on
       plain WHILE changes otherwise-valid CFGs.  Delay ownership until the body
       shape has independently proven this WHILE is a normalized source `for`.
       Plain while/do-while lowering therefore remains on the legacy path; only
       a normalized-for loop can bind the recovered continue target to condition
       re-evaluation. */
    if (continue_label_statement != MINIC_STATEMENT_INVALID && normalized_for) {
        if (context->statement_blocks == NULL ||
            continue_label_statement >= context->statement_block_count ||
            context->statement_blocks[continue_label_statement] != MINIC_CORE_BLOCK_INVALID) {
            return MINIC_CORE_LOWER_ERROR;
        }
        context->statement_blocks[continue_label_statement] = condition_block;
    }
    if (statement->expression == MINIC_EXPRESSION_INVALID && !normalized_for) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
'''
if core.count(needle) != 1:
    raise SystemExit(f'M147 expected one normalized-for validity seam, found {core.count(needle)}')
core = core.replace(needle, replacement, 1)
core_path.write_text(core)

regression = Path('tests/compiler/c0/m147_normalized_for_continue_binding.c')
regression.write_text(r'''static int plain_while_baseline(int n) {
    int sum = 0;
    while (n > 0) {
        n -= 1;
        if (n & 1)
            sum += n;
    }
    return sum;
}

static int normalized_for_continue(int n) {
    int i;
    int sum = 0;
    for (i = 0; i < n; i += 1) {
        if (i & 1)
            continue;
        sum += i;
    }
    return sum;
}

int main(void) {
    return plain_while_baseline(5) == 4 &&
           normalized_for_continue(6) == 6 ? 0 : 1;
}
''')

print('M147 normalized-for-only continue binding staged')
