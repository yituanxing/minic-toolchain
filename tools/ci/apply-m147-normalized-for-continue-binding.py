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

old = '''    if (continue_label_statement != MINIC_STATEMENT_INVALID) {
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
'''
new = '''    /* M147_NORMALIZED_FOR_CONTINUE_BINDING_OWNER: M145/M146 proved that the
       same-span detached-label heuristic must not mutate an ordinary source
       WHILE CFG.  Keep binding at the established post-block-allocation point,
       but require the body-shape recognizers above to have independently proven
       parser-normalized `for` provenance first.  This preserves the valid
       condition_block lifetime/order required by unbounded `for (;;)` while
       leaving plain while/do-while lowering on the baseline path. */
    if (continue_label_statement != MINIC_STATEMENT_INVALID && normalized_for) {
        if (context->statement_blocks == NULL ||
            continue_label_statement >= context->statement_block_count ||
            context->statement_blocks[continue_label_statement] != MINIC_CORE_BLOCK_INVALID) {
            return MINIC_CORE_LOWER_ERROR;
        }
        context->statement_blocks[continue_label_statement] = condition_block;
    }
'''
if core.count(old) != 1:
    raise SystemExit(f'M147 expected one post-allocation continue binding seam, found {core.count(old)}')
core = core.replace(old, new, 1)
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

print('M147 normalized-for-only post-allocation continue binding staged')
