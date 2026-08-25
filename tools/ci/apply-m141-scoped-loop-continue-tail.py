#!/usr/bin/env python3
from pathlib import Path

core_path = Path('src/core/core_lower.c')
core = core_path.read_text()
marker = 'M141_SCOPED_LOOP_CONTINUE_TAIL_OWNER'
if marker in core:
    print('M141 scoped loop continue-tail owner already staged')
    raise SystemExit(0)
if 'M134_UNREACHABLE_TAIL_OWNER' not in core:
    raise SystemExit('M141 requires production M134 unreachable-tail ownership')
if 'M137_DETACHED_LOOP_CONTINUE_OWNER' not in core:
    raise SystemExit('M141 requires staged M137 detached continue-target ownership')

old_decl = '''    const MinicStatement *for_update;
    MinicBlock normalized_for_body;
    MinicBlock normalized_do_while_body;
'''
new_decl = '''    const MinicStatement *for_update;
    MinicBlock normalized_for_body;
    MinicBlock scoped_iteration_body;
    MinicBlock normalized_do_while_body;
'''
if core.count(old_decl) != 1:
    raise SystemExit(f'expected one lower_while normalized block declaration, found {core.count(old_decl)}')
core = core.replace(old_decl, new_decl, 1)

old_selection = '''    } else if (normalized_for_continue_tail(
                   context, statement, body_source, &normalized_for_body)) {
        iteration_source = &normalized_for_body;
        normalized_for = true;
    }
    if (statement->expression == MINIC_EXPRESSION_INVALID && !normalized_for) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
'''
new_selection = '''    } else if (normalized_for_continue_tail(
                   context, statement, body_source, &normalized_for_body)) {
        iteration_source = &normalized_for_body;
        normalized_for = true;
    }
    /* M141_SCOPED_LOOP_CONTINUE_TAIL_OWNER: M137 has already proven that
       continue_label_statement is the unique parser-owned continue target for
       this exact loop and lower_while will bind it to condition_block before
       lowering the body. If the normalized-for view still retains that exact
       synthetic label as its sequential tail, remove only that statement from
       the executable iteration view. This prevents generic LABEL lowering from
       switching context->block_id back to the already-terminated condition
       block and falsely clearing body termination. Ordinary labels and loops
       without an M137-proven target are bit-for-bit unchanged. */
    if (normalized_for && continue_label_statement != MINIC_STATEMENT_INVALID &&
        iteration_source != NULL && iteration_source->statement_count > 0U &&
        iteration_source->statements[iteration_source->statement_count - 1U] ==
            continue_label_statement) {
        scoped_iteration_body = *iteration_source;
        scoped_iteration_body.statement_count -= 1U;
        iteration_source = &scoped_iteration_body;
    }
    if (statement->expression == MINIC_EXPRESSION_INVALID && !normalized_for) {
        return MINIC_CORE_LOWER_UNSUPPORTED;
    }
'''
if core.count(old_selection) != 1:
    raise SystemExit(f'expected one normalized-for selection seam, found {core.count(old_selection)}')
core = core.replace(old_selection, new_selection, 1)
core_path.write_text(core)

regression = Path('tests/compiler/c0/m141_scoped_loop_continue_tail.c')
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

static int ordinary_for_unchanged(int limit) {
    int i;
    int sum = 0;
    for (i = 0; i < limit; i += 1)
        sum += i;
    return sum;
}

int main(void) {
    return for_unbounded_continue(4) == 4 &&
           for_update_continue(6) == 6 &&
           nested_for_continue(3) == 6 &&
           ordinary_for_unchanged(5) == 10 ? 0 : 1;
}
''')

print('M141 scoped loop continue-tail owner and regression staged')
