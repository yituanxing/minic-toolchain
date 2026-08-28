#!/usr/bin/env python3
from pathlib import Path

# M135_TRIGGER_V2: validate non-edge cleanup metadata; materialized control edges
# are already covered by the existing cleanup regression suite.
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
           here when they still carry a nonempty cleanup edge. Parser-materialized
           zero-distance control edges continue through their existing owners.
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

print('M135 cleanup edge classification and non-edge regression staged')
