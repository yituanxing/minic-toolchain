#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[2]
codegen_path = root / 'src/target/riscv64/codegen_function.c'
text = codegen_path.read_text()
old = '''    if (file == NULL || program == NULL || object == NULL || object->name_length == 0U ||
        object->storage_size == 0U || object->alignment == 0U ||
        !minic_riscv64_alignment_power(object->alignment, &alignment_power)) {
        return false;
    }
'''
new = '''    if (file == NULL || program == NULL || object == NULL || object->name_length == 0U ||
        object->alignment == 0U ||
        (object->storage_size == 0U && !object->is_zero_initialized && !object->is_tentative) ||
        !minic_riscv64_alignment_power(object->alignment, &alignment_power)) {
        return false;
    }
'''
if text.count(old) != 1:
    raise SystemExit('zero-sized global codegen anchor missing')
codegen_path.write_text(text.replace(old, new, 1))

source_path = root / 'tests/compiler/c0/gnu_empty_records.c'
source = source_path.read_text()
source += '''\nstatic struct EmptyStruct empty_static_global;\n\nint empty_static_global_address(void) {\n    return &empty_static_global != (void *)0;\n}\n'''
source_path.write_text(source)

run_path = root / 'tests/compiler/c0/run-gnu-empty-records.sh'
run = run_path.read_text()
old_run = '''grep -F '  li a0, 8' "$assembly" >/dev/null\n\nprintf '%s\\n' 'PASS compiler/c0/gnu_empty_records struct-size=0 union-size=0 empty-member-declaration=ignored member-record-size=8 complete=1 layout-sentinel=alignment'\n'''
new_run = '''grep -F '  li a0, 8' "$assembly" >/dev/null\ngrep -F 'empty_static_global:' "$assembly" >/dev/null\ngrep -F '.size empty_static_global, 0' "$assembly" >/dev/null\n\nprintf '%s\\n' 'PASS compiler/c0/gnu_empty_records struct-size=0 union-size=0 empty-member-declaration=ignored member-record-size=8 complete=1 zero-sized-global=1 layout-sentinel=alignment'\n'''
if run.count(old_run) != 1:
    raise SystemExit('empty-record focused anchor missing')
run_path.write_text(run.replace(old_run, new_run, 1))
