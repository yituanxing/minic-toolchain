#!/usr/bin/env python3
from pathlib import Path

core_path = Path('src/core/core_lower.c')
core = core_path.read_text()
marker = 'M142_NONEDGE_CLEANUP_METADATA_OWNER'
if marker in core:
    print('M142 nonedge cleanup metadata owner already staged')
    raise SystemExit(0)
if 'M134_UNREACHABLE_TAIL_OWNER' not in core:
    raise SystemExit('M142 requires production M134 control-flow ownership')

old = '''        if (statement->cleanup_context != statement->cleanup_stop_context &&
            statement->kind != MINIC_STATEMENT_RETURN) {
'''
new = '''        /* M142_NONEDGE_CLEANUP_METADATA_OWNER: cleanup ids describe the
           current lifetime context as well as executable cleanup transitions.
           A plain ASSIGN has no control edge of its own, so crossing cleanup is
           still owned by the eventual RETURN/BREAK/GOTO/scope-exit edge. An
           adjacent parser-internal loop label is likewise target metadata, not
           an executable cleanup edge. Keep every other nonzero-distance shape
           fail-closed; in particular BREAK/GOTO and structured IF/WHILE are not
           generalized here. */
        if (statement->cleanup_context != statement->cleanup_stop_context &&
            statement->kind != MINIC_STATEMENT_RETURN &&
            statement->kind != MINIC_STATEMENT_ASSIGN &&
            !(statement->kind == MINIC_STATEMENT_LABEL &&
              statement_index + 1U < source_block->statement_count &&
              internal_while_label_pair(
                  statement,
                  minic_c0_program_statement(
                      context->body->program,
                      source_block->statements[statement_index + 1U])))) {
'''
if core.count(old) != 1:
    raise SystemExit(f'expected one generic cleanup guard, found {core.count(old)}')
core = core.replace(old, new, 1)
core_path.write_text(core)

regression = Path('tests/compiler/c0/m142_nonedge_cleanup_metadata.c')
regression.write_text(r'''static void cleanup_int(int *value) {
    *value += 1;
}

static int cleanup_assignment(int seed) {
    int guard __attribute__((cleanup(cleanup_int))) = seed;
    int result = 0;
    result = guard + 7;
    return result;
}

static int cleanup_internal_loop_label(int seed) {
    int total = 0;
    int i;
    for (i = 0; i < 3; i += 1) {
        int guard __attribute__((cleanup(cleanup_int))) = i;
        while (guard < 0) {
            guard += 1;
        }
        total += guard;
    }
    return total + seed;
}

int main(void) {
    return cleanup_assignment(2) == 9 &&
           cleanup_internal_loop_label(4) == 7 ? 0 : 1;
}
''')

print('M142 nonedge cleanup metadata owner and regression staged')
