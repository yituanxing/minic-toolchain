#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
type_path = root / "src/frontend/type.c"
run_path = root / "tests/compiler/c0/run.sh"

text = type_path.read_text()
anchor = "bool minic_type_assignment_compatible(MinicType target, MinicType source) {\n"
helper = r'''static bool minic_type_gnu_integer_pointer_signedness_compatible(MinicType target,
                                                                  MinicType source) {
    MinicType target_pointee;
    MinicType source_pointee;
    MinicType target_unqualified;
    MinicType source_unqualified;

    if (target.pointer_depth != 1U || source.pointer_depth != 1U ||
        !minic_type_pointee(target, &target_pointee) ||
        !minic_type_pointee(source, &source_pointee) ||
        !minic_type_unqualified(target_pointee, &target_unqualified) ||
        !minic_type_unqualified(source_pointee, &source_unqualified) ||
        !minic_type_is_integer(target_unqualified) ||
        !minic_type_is_integer(source_unqualified) ||
        minic_type_is_enum(target_unqualified) || minic_type_is_enum(source_unqualified) ||
        target_unqualified.base_kind != MINIC_TYPE_BASE_INT ||
        source_unqualified.base_kind != MINIC_TYPE_BASE_INT ||
        target_unqualified.integer_rank != source_unqualified.integer_rank ||
        target_unqualified.is_plain_char != source_unqualified.is_plain_char ||
        target_unqualified.explicit_alignment != source_unqualified.explicit_alignment) {
        return false;
    }
    if (minic_type_is_const(source_pointee) && !minic_type_is_const(target_pointee)) {
        return false;
    }
    if (minic_type_is_volatile(source_pointee) && !minic_type_is_volatile(target_pointee)) {
        return false;
    }
    return true;
}

'''
if helper not in text:
    if text.count(anchor) != 1:
        raise SystemExit("assignment compatibility anchor changed")
    text = text.replace(anchor, helper + anchor, 1)

old = r'''    return minic_type_equal(unqualified_target, unqualified_source) ||
           minic_type_pointer_qualification_compatible(unqualified_target, unqualified_source) ||
           minic_type_void_object_pointer_compatible(unqualified_target, unqualified_source);
'''
new = r'''    return minic_type_equal(unqualified_target, unqualified_source) ||
           minic_type_pointer_qualification_compatible(unqualified_target, unqualified_source) ||
           minic_type_gnu_integer_pointer_signedness_compatible(unqualified_target,
                                                                 unqualified_source) ||
           minic_type_void_object_pointer_compatible(unqualified_target, unqualified_source);
'''
if new not in text:
    if text.count(old) != 1:
        raise SystemExit("assignment compatibility return changed")
    text = text.replace(old, new, 1)
type_path.write_text(text)

gate = r'''
MINIC="$minic" HOST_CC="$host_cc" BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
  sh "$root/tests/compiler/c0/run-gnu-pointer-signedness.sh"
'''
run = run_path.read_text()
if gate.strip() not in run:
    if not run.endswith("\n"):
        run += "\n"
    run += gate
run_path.write_text(run)

print("materialized bounded GNU same-rank integer pointer signedness compatibility")
