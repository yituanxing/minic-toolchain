#!/usr/bin/env python3
from pathlib import Path

core_path = Path('src/core/core_lower.c')
core = core_path.read_text()
marker = 'M146_OPPORTUNISTIC_DETACHED_CONTINUE_OWNER'
if marker in core:
    print('M146 opportunistic detached-continue owner already staged')
    raise SystemExit(0)
if 'M137_DETACHED_LOOP_CONTINUE_OWNER' not in core:
    raise SystemExit('M146 requires staged M137 resolver')

old = '''            case MINIC_STATEMENT_WHILE: {
                MinicStatementId detached_continue_label;

                if (!core_resolve_detached_while_continue_label(
                        context, statement, &detached_continue_label)) {
                    status = MINIC_CORE_LOWER_UNSUPPORTED;
                    break;
                }
                status = lower_while(
                    context, statement, detached_continue_label, &statement_terminated);
                break;
            }
'''
new = '''            case MINIC_STATEMENT_WHILE: {
                MinicStatementId detached_continue_label;

                /* M146_OPPORTUNISTIC_DETACHED_CONTINUE_OWNER: detached parser
                   metadata recovery is an optional refinement of ordinary WHILE
                   lowering, never a new validity gate.  M137 can fail discovery
                   when the loop subtree has ambiguous same-span internal labels
                   or when its graph cannot be classified conservatively.  In
                   either case preserve the pre-M137 behavior by lowering with no
                   detached target.  Only a positively resolved unique target is
                   allowed to change the loop CFG. */
                detached_continue_label = MINIC_STATEMENT_INVALID;
                if (!core_resolve_detached_while_continue_label(
                        context, statement, &detached_continue_label)) {
                    detached_continue_label = MINIC_STATEMENT_INVALID;
                }
                status = lower_while(
                    context, statement, detached_continue_label, &statement_terminated);
                break;
            }
'''
if core.count(old) != 1:
    raise SystemExit(f'M146 expected one M137 standalone while seam, found {core.count(old)}')
core = core.replace(old, new, 1)
core_path.write_text(core)

regression = Path('tests/compiler/c0/m146_opportunistic_detached_continue.c')
regression.write_text(r'''static int ambiguous_loop_fallback(int n) {
    int total = 0;
    while (n > 0) {
        n -= 1;
        if (n == 5)
            continue;
        if (n == 2)
            break;
        total += n;
    }
    return total;
}

static int nested_continue(int n) {
    int total = 0;
    while (n > 0) {
        int inner = n;
        while (inner > 0) {
            inner -= 1;
            if (inner & 1)
                continue;
            total += 1;
        }
        n -= 1;
    }
    return total;
}

int main(void) {
    return ambiguous_loop_fallback(6) == 7 && nested_continue(3) == 2 ? 0 : 1;
}
''')

print('M146 opportunistic detached-continue fallback staged')
