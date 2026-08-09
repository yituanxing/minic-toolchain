#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:80]!r}")
    target.write_text(text.replace(old, new, 1))


# C lvalue conversion removes top-level qualifiers from scalar values.  The frontend keeps
# qualifiers on the member lvalue itself (which is useful for assignment diagnostics), so the
# RV64 conditional join must treat a qualification-only scalar difference as representation-
# preserving once the branch is read as a value.  This is generic C value semantics, not a
# Parson-specific exception.
replace_once(
    "src/target/riscv64/codegen_expression.c",
    '''static bool minic_riscv64_emit_conditional_result_conversion(FILE *file,
                                                             MinicType source_type,
                                                             MinicType result_type) {
    if (minic_type_equal(source_type, result_type)) {
        return true;
    }
    if (minic_type_is_pointer(source_type) && minic_type_is_pointer(result_type)) {
        return true;
    }
''',
    '''static bool minic_riscv64_emit_conditional_result_conversion(FILE *file,
                                                             MinicType source_type,
                                                             MinicType result_type) {
    MinicType unqualified_source;
    MinicType unqualified_result;

    if (minic_type_equal(source_type, result_type)) {
        return true;
    }
    if (minic_type_unqualified(source_type, &unqualified_source) &&
        minic_type_unqualified(result_type, &unqualified_result) &&
        minic_type_equal(unqualified_source, unqualified_result)) {
        return true;
    }
    if (minic_type_is_pointer(source_type) && minic_type_is_pointer(result_type)) {
        return true;
    }
''',
)

print("staged qualification-only conditional value conversion")
