#!/usr/bin/env python3
from pathlib import Path

# M140_PRODUCTIZER_TRIGGER_V1: synchronize after registering the productizer.
core_path = Path('src/core/core_lower.c')
core = core_path.read_text()
marker = 'M140_NORMALIZED_FOR_CONTINUE_TAIL_OWNER'
if marker in core:
    print('M140 normalized-for continue-tail owner already staged')
    raise SystemExit(0)
if 'M137_DETACHED_LOOP_CONTINUE_OWNER' not in core:
    raise SystemExit('M140 requires staged M137 detached continue target ownership')

old_continue = '''    *iteration_body = *body;
    if (!core_cleanup_edge_is_empty(continue_label)) {
        iteration_body->statement_count -= 1U;
    }
    return true;
}
'''
new_continue = '''    /* M140_NORMALIZED_FOR_CONTINUE_TAIL_OWNER: the parser-owned continue
       label is target metadata, not an executable sequential statement. M137
       binds any reachable explicit continue directly to the loop condition
       block before the iteration body is lowered. Keeping this synthetic label
       in the body would switch context->block_id back to that already-terminated
       condition block and then falsely report body_terminated=false. Strip the
       label from the normalized body regardless of cleanup metadata; cleanup-
       carrying continue edges themselves remain fail-closed in their GOTO owner. */
    *iteration_body = *body;
    iteration_body->statement_count -= 1U;
    return true;
}
'''
if core.count(old_continue) != 1:
    raise SystemExit(f'expected one normalized-for continue tail, found {core.count(old_continue)}')
core = core.replace(old_continue, new_continue, 1)

old_update = '''    *iteration_body = *body;
    iteration_body->statement_count -=
        core_cleanup_edge_is_empty(continue_label) ? 1U : 2U;
    *update_statement = update;
    return true;
}
'''
new_update = '''    *iteration_body = *body;
    /* The update and its synthetic continue label are both outside the
       executable iteration body. Explicit continue reaches condition_block via
       M137's pre-bound statement target; ordinary fallthrough reaches the same
       block through lower_while's backedge after the update is evaluated. */
    iteration_body->statement_count -= 2U;
    *update_statement = update;
    return true;
}
'''
if core.count(old_update) != 1:
    raise SystemExit(f'expected one normalized-for update tail, found {core.count(old_update)}')
core = core.replace(old_update, new_update, 1)
core_path.write_text(core)

regression = Path('tests/compiler/c0/m140_normalized_for_continue_tail.c')
regression.write_text(r'''static int for_unbounded_continue(int limit) {
    int i = 0;
    for (;;) {
        i += 1;
        if (i < limit)
            continue;
        return i;
    }
}

static int for_update_continue(int limit) {
    int i = 0;
    int sum = 0;
    for (; i < limit; i += 1) {
        if (i & 1)
            continue;
        sum += i;
    }
    return sum;
}

static int nested_for_continue(int limit) {
    int outer = 0;
    int total = 0;
    for (; outer < limit; outer += 1) {
        int inner = 0;
        for (;;) {
            inner += 1;
            if (inner < 2)
                continue;
            break;
        }
        total += inner;
    }
    return total;
}

int main(void) {
    return for_unbounded_continue(4) == 4 &&
           for_update_continue(6) == 6 &&
           nested_for_continue(3) == 6 ? 0 : 1;
}
''')

print('M140 normalized-for continue-tail owner and regression staged')
