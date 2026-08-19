#!/usr/bin/env python3
from pathlib import Path

ast_global = Path("src/frontend/ast_global.c")
text = ast_global.read_text()
function_start = text.index("static bool global_object_member_path_type(")
function_end = text.index("\nstatic bool ", function_start + 1)
section = text[function_start:function_end]
old = '''        if (field == NULL || field->element_count != 1U || field->is_bit_field ||
            field->is_flexible_array) {
            return false;
        }
        type = field->type;
'''
new = '''        if (field == NULL || field->element_count == 0U || field->is_bit_field ||
            field->is_flexible_array ||
            (field->is_array && depth + 1U != member_depth) ||
            (!field->is_array && field->element_count != 1U)) {
            return false;
        }
        type = field->type;
'''
if section.count(old) != 1:
    raise SystemExit(f"unexpected AST relocation path validator shape count={section.count(old)}")
section = section.replace(old, new, 1)
ast_global.write_text(text[:function_start] + section + text[function_end:])

data_layout = Path("src/target/data_layout.c")
text = data_layout.read_text()
function_start = text.index("bool minic_data_layout_global_relocation_target_addend(")
section = text[function_start:]
old = '''        if (field == NULL || field->element_count != 1U || field->is_bit_field ||
            field->is_flexible_array ||
            !minic_data_layout_record_field_offset(
                layout, program, record, relocation->target_member_indices[depth], &field_offset) ||
            result > SIZE_MAX - field_offset) {
'''
new = '''        if (field == NULL || field->element_count == 0U || field->is_bit_field ||
            field->is_flexible_array ||
            (field->is_array && depth + 1U != relocation->target_member_depth) ||
            (!field->is_array && field->element_count != 1U) ||
            !minic_data_layout_record_field_offset(
                layout, program, record, relocation->target_member_indices[depth], &field_offset) ||
            result > SIZE_MAX - field_offset) {
'''
if section.count(old) != 1:
    raise SystemExit(f"unexpected DataLayout relocation path resolver shape count={section.count(old)}")
section = section.replace(old, new, 1)
data_layout.write_text(text[:function_start] + section)

print("materialized terminal array-field relocation path semantics")
