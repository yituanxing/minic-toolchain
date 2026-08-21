#!/usr/bin/env python3
"""Replay active-union metadata before aggregate-array relocations."""
from pathlib import Path


# Aggregate-array actions capture initializer values, relocations, and active-union
# selections transactionally. Relocation type validation depends on the selected
# union member, so replay semantic shape metadata before symbolic relocations.
path = Path("src/frontend/parser_global.c")
text = path.read_text()
reloc_marker = "    for (index = 0U; index < action->relocation_count; ++index) {\n"
selection_marker = "    for (index = 0U; index < action->union_selection_count; ++index) {\n"
return_marker = "    return true;\n}\n\nstatic bool append_static_chained_array_designator_value"
reloc_start = text.find(reloc_marker)
selection_start = text.find(selection_marker, reloc_start)
return_start = text.find(return_marker, selection_start)
if reloc_start < 0 or selection_start < 0 or return_start < 0:
    raise SystemExit("cannot locate aggregate-array action replay blocks")
reloc_block = text[reloc_start:selection_start]
selection_block = text[selection_start:return_start]

if "minic_c0_global_object_select_union_member_with_span" not in selection_block:
    old = '''        if (!minic_c0_global_object_select_union_member(parser->program,
                                                        object_id,
                                                        initializer_slot,
                                                        selection->record_id,
                                                        selection->field_index)) {
'''
    new = '''        if (!minic_c0_global_object_select_union_member_with_span(parser->program,
                                                                  object_id,
                                                                  initializer_slot,
                                                                  selection->record_id,
                                                                  selection->field_index,
                                                                  selection->initializer_span)) {
'''
    if old not in selection_block:
        raise SystemExit("aggregate-array union selection replay anchor not found")
    selection_block = selection_block.replace(old, new, 1)

text = text[:reloc_start] + selection_block + reloc_block + text[return_start:]
path.write_text(text)

# Regression: the canonical union member is a function pointer while the selected
# member is a data pointer. Replaying the string relocation before the active-member
# selection must fail type validation; the correct replay order must compile it.
test_path = Path("tests/compiler/c0/static_aggregate_array_designators.c")
test = test_path.read_text()
old = '''struct relocation_op {
    const char *lsm;
};

struct relocation_row {
    int *target;
    struct relocation_op op;
};
'''
new = '''union relocation_op {
    int (*show)(void);
    const char *lsm;
};

struct relocation_row {
    int *target;
    union relocation_op op;
};
'''
if new not in test:
    if old not in test:
        raise SystemExit("aggregate-array union relocation regression anchor not found")
    test = test.replace(old, new, 1)
test_path.write_text(test)

run_path = Path("tests/compiler/c0/run-static-aggregate-array-designators.sh")
run = run_path.read_text()
old_pass = "relocation-owner=aggregate-scalar"
new_pass = "relocation-owner=aggregate-scalar union-before-relocation=1"
if new_pass not in run:
    if old_pass not in run:
        raise SystemExit("aggregate-array regression pass marker not found")
    run = run.replace(old_pass, new_pass, 1)
run_path.write_text(run)
