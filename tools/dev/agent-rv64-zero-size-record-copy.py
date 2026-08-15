#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected exactly one match, got {count}")
    path.write_text(text.replace(old, new, 1))


support = Path("src/target/riscv64/codegen_support.c")
replace_once(
    support,
    '''    if (object == NULL ||\n        !minic_riscv64_function_layout_local_offset(\n            function_layout, function, local_id, &object_offset) ||\n        function_layout->local_storage_size == 0U ||\n        object_offset >= function_layout->local_storage_size) {\n        return false;\n    }\n''',
    '''    if (object == NULL ||\n        !minic_riscv64_function_layout_local_offset(\n            function_layout, function, local_id, &object_offset) ||\n        object_offset > function_layout->local_storage_size) {\n        return false;\n    }\n''',
)

codegen = Path("src/target/riscv64/codegen_expression.c")
replace_once(
    codegen,
    '''    if (record == NULL || !record->is_complete ||\n        !minic_riscv64_type_layout(program, target->type, &storage_size, &temporary_size) ||\n        storage_size == 0U || storage_size > SIZE_MAX - 15U) {\n        return false;\n    }\n    temporary_size = (storage_size + 15U) & ~(size_t)15U;\n\n    return minic_riscv64_emit_record_value_temporary(\n''',
    '''    if (record == NULL || !record->is_complete ||\n        !minic_riscv64_type_layout(program, target->type, &storage_size, &temporary_size) ||\n        storage_size > SIZE_MAX - 15U) {\n        return false;\n    }\n    if (storage_size == 0U) {\n        return minic_c0_record_value_is_address_backed(program, source_id) &&\n               minic_riscv64_emit_address_backed_record_value(\n                   file, program, function, function_layout, source_id) &&\n               minic_riscv64_emit_lvalue_address(\n                   file, program, function, function_layout, target_id);\n    }\n    temporary_size = (storage_size + 15U) & ~(size_t)15U;\n\n    return minic_riscv64_emit_record_value_temporary(\n''',
)

fixture = Path("tests/compiler/c0/gnu_empty_records.c")
fixture.write_text(
    fixture.read_text()
    + '''\nstruct EmptyHolder {\n    struct EmptyStruct cookie;\n};\n\nstatic struct EmptyStruct *empty_source(struct EmptyStruct *value) {\n    return value;\n}\n\nstatic struct EmptyStruct *empty_target(struct EmptyStruct *value) {\n    return value;\n}\n\nvoid empty_record_statement_copy(struct EmptyHolder *holder) {\n    holder->cookie = ({\n        struct EmptyStruct cookie = {};\n        cookie;\n    });\n}\n\nvoid empty_record_lvalue_copy(struct EmptyStruct *target, struct EmptyStruct *source) {\n    *empty_target(target) = *empty_source(source);\n}\n'''
)

runner = Path("tests/compiler/c0/run-gnu-empty-records.sh")
replace_once(
    runner,
    '''for symbol in empty_struct_size empty_union_size empty_member_record_size empty_identity; do\n''',
    '''for symbol in empty_struct_size empty_union_size empty_member_record_size empty_identity \\\n              empty_record_statement_copy empty_record_lvalue_copy; do\n''',
)
replace_once(
    runner,
    '''grep -F '  li a0, 8' "$assembly" >/dev/null\n\nprintf '%s\\n' 'PASS compiler/c0/gnu_empty_records struct-size=0 union-size=0 empty-member-declaration=ignored member-record-size=8 complete=1 layout-sentinel=alignment'\n''',
    '''grep -F '  li a0, 8' "$assembly" >/dev/null\ngrep -F '  call empty_source' "$assembly" >/dev/null\ngrep -F '  call empty_target' "$assembly" >/dev/null\nsource_line=$(grep -n -m1 '  call empty_source' "$assembly" | cut -d: -f1)\ntarget_line=$(grep -n -m1 '  call empty_target' "$assembly" | cut -d: -f1)\ntest "$source_line" -lt "$target_line"\n\nprintf '%s\\n' 'PASS compiler/c0/gnu_empty_records struct-size=0 union-size=0 empty-member-declaration=ignored member-record-size=8 zero-copy=statement+rvalue addressable-zero-local=1 side-effects=source+target complete=1 layout-sentinel=alignment'\n''',
)
