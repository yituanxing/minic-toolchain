#!/usr/bin/env python3
"""Materialize zero-length record-field ownership in the recursive RV64 emitter."""
from pathlib import Path

codegen_path = Path("src/target/riscv64/codegen_function.c")
text = codegen_path.read_text()

before = '''            field = minic_c0_record_field(record, field_index);
            if (field == NULL || field->element_count == 0U) {
                return false;
            }
            if (field->is_flexible_array) {
'''
after = '''            field = minic_c0_record_field(record, field_index);
            if (field == NULL) {
                return false;
            }
            if (field->is_zero_length_array) {
                /* GNU zero-length record fields own no semantic initializer slots
                 * and no storage bytes. DataLayout already places the following
                 * field at the same cursor, so recursive emission must skip the
                 * field rather than materializing one element. */
                continue;
            }
            if (field->element_count == 0U) {
                return false;
            }
            if (field->is_flexible_array) {
'''
if after not in text:
    if text.count(before) != 1:
        raise SystemExit("recursive record-field emitter anchor not found uniquely")
    text = text.replace(before, after, 1)
codegen_path.write_text(text)

case_path = Path("tests/compiler/c0/static_zero_length_record_field_relocation.c")
case_path.write_text(
    '''static int target = 7;\n\n'''
    '''struct payload {\n    long a;\n    long b;\n    long c;\n    long d;\n    long e;\n    long f;\n    long g;\n};\n\n'''
    '''struct holder {\n    int before;\n    struct payload zero[0];\n    int *pointer;\n    int after;\n};\n\n'''
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
needle = '''"$minic" -S "$work/zero.i" -o "$work/zero.s"
grep -F 'vm_numa_event' "$work/zero.s" >/dev/null || true
printf '%s\\n' 'PASS compiler/c0/gnu_zero_length_array extern=1 incomplete-to-zero=1 sizeof=0 decay=1 type-identity=complete-zero'
'''
replacement = '''"$minic" -S "$work/zero.i" -o "$work/zero.s"
grep -F 'vm_numa_event' "$work/zero.s" >/dev/null || true
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/static_zero_length_record_field_relocation.c" -o "$work/record-field.i"
"$minic" -S "$work/record-field.i" -o "$work/record-field.s"
test -s "$work/record-field.s"
grep -Fq 'target' "$work/record-field.s"
printf '%s\\n' 'PASS compiler/c0/gnu_zero_length_array extern=1 incomplete-to-zero=1 sizeof=0 decay=1 type-identity=complete-zero record-field=zero-storage+following-relocation'
'''
if replacement not in run_text:
    if run_text.count(needle) != 1:
        raise SystemExit("GNU zero-length regression anchor not found uniquely")
    run_text = run_text.replace(needle, replacement, 1)
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
