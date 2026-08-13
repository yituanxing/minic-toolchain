#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
codegen_path = root / 'src/target/riscv64/codegen_function.c'
text = codegen_path.read_text()
old_guard = '''    if (file == NULL || program == NULL || object == NULL || object->name_length == 0U ||
        object->alignment == 0U ||
        (object->storage_size == 0U && !object->is_zero_initialized && !object->is_tentative) ||
        !minic_riscv64_alignment_power(object->alignment, &alignment_power)) {
        return false;
    }

    directive = NULL;
    scalar_width = 0U;
    if (object->is_zero_initialized || object->is_tentative) {
'''
new_guard = '''    if (file == NULL || program == NULL || object == NULL || object->name_length == 0U ||
        object->alignment == 0U ||
        (object->storage_size == 0U &&
         (object->initializer_count != 0U || object->relocation_count != 0U)) ||
        !minic_riscv64_alignment_power(object->alignment, &alignment_power)) {
        return false;
    }

    directive = NULL;
    scalar_width = 0U;
    if (object->storage_size == 0U) {
        /* A verified zero-sized GNU object has no storage payload to encode. */
    } else if (object->is_zero_initialized || object->is_tentative) {
'''
if text.count(old_guard) != 1:
    raise SystemExit('zero-sized global classification anchor missing')
text = text.replace(old_guard, new_guard, 1)

old_emit = '''    if (minic_type_is_record(object->type) && object->initializer_count != 0U) {
        if (!minic_riscv64_emit_record_values(file, program, object)) {
            return false;
        }
    } else if (object->relocation_count != 0U) {
'''
new_emit = '''    if (object->storage_size == 0U) {
        /* Symbol metadata and .size 0 are sufficient; emit no storage bytes. */
    } else if (minic_type_is_record(object->type) && object->initializer_count != 0U) {
        if (!minic_riscv64_emit_record_values(file, program, object)) {
            return false;
        }
    } else if (object->relocation_count != 0U) {
'''
if text.count(old_emit) != 1:
    raise SystemExit('zero-sized global emission anchor missing')
codegen_path.write_text(text.replace(old_emit, new_emit, 1))

source_path = root / 'tests/compiler/c0/gnu_empty_records.c'
source = source_path.read_text()
source += '''\nint empty_static_initialized_address(void) {\n    static struct EmptyStruct value = {};\n    return &value != (void *)0;\n}\n'''
source_path.write_text(source)

run_path = root / 'tests/compiler/c0/run-gnu-empty-records.sh'
run = run_path.read_text()
old = '''grep -F '.size empty_static_global, 0' "$assembly" >/dev/null\n\nprintf '%s\\n' 'PASS compiler/c0/gnu_empty_records struct-size=0 union-size=0 empty-member-declaration=ignored member-record-size=8 complete=1 zero-sized-global=1 layout-sentinel=alignment'\n'''
new = '''grep -F '.size empty_static_global, 0' "$assembly" >/dev/null\ngrep -E '\\.size __minic_static_local_[0-9]+_[0-9]+, 0' "$assembly" >/dev/null\n\nprintf '%s\\n' 'PASS compiler/c0/gnu_empty_records struct-size=0 union-size=0 empty-member-declaration=ignored member-record-size=8 complete=1 zero-sized-global=implicit+explicit layout-sentinel=alignment'\n'''
if run.count(old) != 1:
    raise SystemExit('empty record v2 gate anchor missing')
run_path.write_text(run.replace(old, new, 1))
