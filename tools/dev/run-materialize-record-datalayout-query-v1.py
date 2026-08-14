#!/usr/bin/env python3
from pathlib import Path

primary = Path("tools/dev/materialize-record-datalayout-query-v1.py")
source = primary.read_text(encoding="utf-8")
old_block = '''old = """            if (field == NULL || field->element_count != 1U ||
                !minic_type_is_integer(field->type) ||
                !minic_riscv64_type_layout(program, field->type, &field_size, &field_alignment)) {
                return false;
            }
            (void)field_alignment;
            if (field->storage_offset > element_size ||
                field_size > element_size - field->storage_offset) {
                return false;
            }
            field_offset = element_base + field->storage_offset;
"""
new = """            if (field == NULL || field->element_count != 1U ||
                !minic_type_is_integer(field->type) ||
                !minic_riscv64_type_layout(program, field->type, &field_size, &field_alignment) ||
                !minic_data_layout_record_field_offset(minic_default_data_layout(),
                                                       program,
                                                       record,
                                                       field_index,
                                                       &field_offset)) {
                return false;
            }
            (void)field_alignment;
            if (field_offset > element_size || field_size > element_size - field_offset) {
                return false;
            }
            field_offset = element_base + field_offset;
"""
text = replace_once(text, old, new, "record array field offset")
'''
new_block = '''old = """            (void)field_alignment;
            if (field->storage_offset > element_size ||
                field_size > element_size - field->storage_offset) {
                return false;
            }
            field_offset = element_base + field->storage_offset;
"""
new = """            (void)field_alignment;
            if (!minic_data_layout_record_field_offset(minic_default_data_layout(),
                                                       program,
                                                       record,
                                                       field_index,
                                                       &field_offset) ||
                field_offset > element_size || field_size > element_size - field_offset) {
                return false;
            }
            field_offset = element_base + field_offset;
"""
text = replace_once(text, old, new, "record array field offset")
'''
if source.count(old_block) != 1:
    raise SystemExit("record array staging block changed")
source = source.replace(old_block, new_block, 1)
exec(compile(source, str(primary), "exec"), {"__name__": "__main__", "__file__": str(primary)})

path = Path("src/target/riscv64/codegen_expression.c")
text = path.read_text(encoding="utf-8")
include = '#include "target/data_layout.h"\n'
if include not in text:
    anchor = '#include "target/riscv64/codegen_internal.h"\n'
    if text.count(anchor) != 1:
        raise SystemExit("codegen_expression.c: include anchor changed")
    text = text.replace(anchor, anchor + include, 1)
path.write_text(text.rstrip() + "\n", encoding="utf-8")

print("NORMALIZED record-datalayout-query-v1")
