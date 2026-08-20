#!/usr/bin/env python3
"""Normalize captured aggregate-array relocations to the destination array owner."""
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, found {count}")
    return text.replace(old, new, 1)


# A captured aggregate-array action stores relocation indexes relative to its
# flattened initializer payload.  On replay the owner is the destination array,
# not the temporary record subobject that originally produced the relocation.
# Keep the target metadata intact, but canonicalize the location to the array's
# aggregate-scalar slot namespace before calling the GlobalObject relocation API.
path = Path("src/frontend/parser_global.c")
text = path.read_text()
start_marker = "    for (index = 0U; index < action->relocation_count; ++index) {\n"
end_marker = "    for (index = 0U; index < action->union_selection_count; ++index) {\n"
start = text.find(start_marker)
end = text.find(end_marker, start)
if start < 0 or end < 0:
    raise SystemExit("cannot locate static aggregate array relocation replay block")
block = text[start:end]
old = "relocation->location_kind"
count = block.count(old)
if count == 0:
    if "MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR" in block:
        print("aggregate-array relocation owner already normalized")
    else:
        raise SystemExit("aggregate-array relocation replay block has unexpected shape")
else:
    block = block.replace(old, "MINIC_GLOBAL_RELOCATION_LOCATION_AGGREGATE_SCALAR")
    text = text[:start] + block + text[end:]
    path.write_text(text)
    print(f"normalized {count} aggregate-array relocation replay call sites")

# Extend the existing permanent aggregate-array regression with the Linux-shaped
# combination that exposed the ownership bug: a direct object relocation plus a
# string relocation nested inside a record member of an array element.
test_path = Path("tests/compiler/c0/static_aggregate_array_designators.c")
test = test_path.read_text()
addition = '''\nstatic int relocation_target;\n\nstruct relocation_op {\n    const char *lsm;\n};\n\nstruct relocation_row {\n    int *target;\n    struct relocation_op op;\n};\n\nstatic const struct relocation_row relocation_records[] = {\n    {.target = &relocation_target, .op = {.lsm = "apparmor"}},\n};\n'''
if "static const struct relocation_row relocation_records[]" not in test:
    marker = "\nint main(void) {\n"
    if marker not in test:
        raise SystemExit("aggregate-array regression insertion marker not found")
    test = test.replace(marker, addition + marker, 1)
    test_path.write_text(test)

run_path = Path("tests/compiler/c0/run-static-aggregate-array-designators.sh")
run = run_path.read_text()
run_addition = '''grep -F '.size relocation_records, 16' "$work/static_aggregate_array_designators.s" >/dev/null\ngrep -F '  .dword relocation_target' "$work/static_aggregate_array_designators.s" >/dev/null\ngrep -F '  .dword .Lminic_string_' "$work/static_aggregate_array_designators.s" >/dev/null\n'''
if ".size relocation_records, 16" not in run:
    marker = "grep -F '  .word 52' \"$work/static_aggregate_array_designators.s\" >/dev/null\n"
    if marker not in run:
        raise SystemExit("aggregate-array run regression marker not found")
    run = run.replace(marker, marker + run_addition, 1)
    old_pass = "PASS compiler/c0/static_aggregate_array_designators inferred-bound=designator-extent nested-field=1 compound-literal=1 backward=fail-closed range=shared-owner"
    new_pass = "PASS compiler/c0/static_aggregate_array_designators inferred-bound=designator-extent nested-field=1 compound-literal=1 relocation-owner=aggregate-scalar backward=fail-closed range=shared-owner"
    if old_pass in run:
        run = run.replace(old_pass, new_pass, 1)
    run_path.write_text(run)
