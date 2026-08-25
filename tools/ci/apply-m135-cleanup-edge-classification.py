#!/usr/bin/env python3
from pathlib import Path

# M135_TRIGGER_V1: keep a post-workflow-registration change so pull_request CI runs it.
core_path = Path('src/core/core_lower.c')
core = core_path.read_text()
marker = 'M135_CLEANUP_EDGE_CLASSIFICATION'
if marker in core:
    print('M135 cleanup edge classification already staged')
    raise SystemExit(0)

old = '''        if (statement->cleanup_context != statement->cleanup_stop_context &&
            statement->kind != MINIC_STATEMENT_RETURN) {
'''
new = '''        /* M135_CLEANUP_EDGE_CLASSIFICATION: cleanup ids are also lifetime
           metadata on ordinary statements generated inside a cleanup scope.
           They imply executable cleanup only on control-transfer edges. RETURN
           has its dedicated lowering owner above; BREAK/GOTO remain fail-closed
           here until their Core branches can materialize cleanup expressions.
           ASSIGN/LABEL/ordinary statements must not be rejected merely because
           parser metadata records current-scope -> root. */
        if (statement->cleanup_context != statement->cleanup_stop_context &&
            (statement->kind == MINIC_STATEMENT_BREAK ||
             statement->kind == MINIC_STATEMENT_GOTO)) {
'''
if core.count(old) != 1:
    raise SystemExit(f'expected one generic cleanup guard, found {core.count(old)}')
core = core.replace(old, new, 1)
core_path.write_text(core)

positive = Path('tests/compiler/c0/m135_cleanup_metadata_nonedge.c')
positive.write_text(r'''struct pair {
    int first;
    int second;
};

static void cleanup_int(int *value) {
    *value = *value + 1;
}

int cleanup_record_assignment(int seed) {
    int guard __attribute__((cleanup(cleanup_int))) = seed;
    struct pair value;

    value = (struct pair){ guard, 7 };
    return value.first + value.second;
}

int cleanup_scope_label(int seed) {
    int guard __attribute__((cleanup(cleanup_int))) = seed;

local_label:
    if (guard < 0) {
        guard = 0;
        goto local_label;
    }
    return guard;
}
''')

negative_break = Path('tests/compiler/c0/m135_cleanup_break_edge.c')
negative_break.write_text(r'''static void cleanup_int(int *value) {
    *value = *value + 1;
}

int cleanup_break_edge(void) {
    for (int guard __attribute__((cleanup(cleanup_int))) = 1; ; ) {
        if (guard) {
            break;
        }
    }
    return 0;
}
''')

negative_goto = Path('tests/compiler/c0/m135_cleanup_goto_edge.c')
negative_goto.write_text(r'''static void cleanup_int(int *value) {
    *value = *value + 1;
}

int cleanup_goto_edge(int take) {
    {
        int guard __attribute__((cleanup(cleanup_int))) = take;
        if (guard) {
            goto outside;
        }
    }
outside:
    return 0;
}
''')

print('M135 cleanup edge classification and regressions staged')
