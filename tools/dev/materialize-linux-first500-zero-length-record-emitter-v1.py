#!/usr/bin/env python3
"""Materialize zero-length and flexible record-field ownership in the recursive RV64 emitter."""
from pathlib import Path

codegen_path = Path("src/target/riscv64/codegen_function.c")
text = codegen_path.read_text()

premature_count_check = '''            if (field->element_count == 0U) {
                return false;
            }
            if (field->is_flexible_array) {
'''
flexible_first = '''            if (field->is_flexible_array) {
'''
if premature_count_check in text:
    if text.count(premature_count_check) != 1:
        raise SystemExit("recursive FAM/count-check anchor not found uniquely")
    text = text.replace(premature_count_check, flexible_first, 1)

post_fam_anchor = '''                continue;
            }
            if (field->is_bit_field) {
'''
post_fam_fixed = '''                continue;
            }
            if (field->element_count == 0U) {
                return false;
            }
            if (field->is_bit_field) {
'''
if post_fam_fixed not in text:
    if text.count(post_fam_anchor) != 1:
        raise SystemExit("recursive post-FAM anchor not found uniquely")
    text = text.replace(post_fam_anchor, post_fam_fixed, 1)
codegen_path.write_text(text)

case_path = Path("tests/compiler/c0/static_zero_length_record_field_relocation.c")
case_path.write_text(
    '''static int target = 7;\n\n'''
    '''struct payload {\n    long a;\n    long b;\n    long c;\n    long d;\n    long e;\n    long f;\n    long g;\n};\n\n'''
    '''struct holder {\n    int before;\n    struct payload zero[0];\n    int *pointer;\n    int after;\n    int *tail[];\n};\n\n'''
    '''static struct holder state = {\n'''
    '''    .before = 3,\n'''
    '''    .pointer = &target,\n'''
    '''    .after = 5,\n'''
    '''};\n\n'''
    '''int main(void) {\n'''
    '''    return (state.before == 3 && state.pointer == &target &&\n'''
    '''            *state.pointer == 7 && state.after == 5)\n'''
    '''               ? 0\n'''
    '''               : 1;\n'''
    '''}\n'''
)

run_path = Path("tests/compiler/c0/run-gnu-zero-length-array.sh")
run_text = run_path.read_text()
old_pass = "record-field=zero-storage+following-relocation"
new_pass = "record-field=zero-storage+following-relocation nested-fam=zero-tail"
if old_pass in run_text:
    run_text = run_text.replace(old_pass, new_pass, 1)
run_path.write_text(run_text)

runtime_path = Path("tests/compiler/c0/run-runtime.sh")
runtime_text = runtime_path.read_text()
runtime_line = "run_case static_zero_length_record_field_relocation 0 static_zero_length_record_field_relocation\n"
if runtime_line not in runtime_text:
    anchor = "run_case static_union_active_member_relocation 0 static_union_active_member_relocation\n"
    if anchor not in runtime_text:
        anchor = "run_case inferred_static_unsigned_char_list 0 inferred_static_unsigned_char_list\n"
    if runtime_text.count(anchor) != 1:
        raise SystemExit("runtime zero-length record-field anchor not found uniquely")
    runtime_text = runtime_text.replace(anchor, anchor + runtime_line, 1)
runtime_path.write_text(runtime_text)
