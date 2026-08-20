#!/usr/bin/env python3
"""Materialize the zero-storage/zero-slot invariant for GNU zero-length arrays."""
from pathlib import Path

# Generic type slot cardinality: a zero-length array contributes no scalar slots,
# both as a materialized array type and as a record field.
ast_path = Path("src/frontend/ast.c")
ast = ast_path.read_text()
old_array = '''        array_type = minic_c0_program_array_type(program, type.array_type_id);
        if (array_type == NULL || array_type->element_count == 0U ||
            !minic_c0_type_initializer_slot_count_impl(
                program, array_type->element_type, &element_slots) ||
'''
new_array = '''        array_type = minic_c0_program_array_type(program, type.array_type_id);
        if (array_type == NULL) {
            return false;
        }
        if (array_type->is_zero_length) {
            *slot_count = 0U;
            return true;
        }
        if (array_type->element_count == 0U ||
            !minic_c0_type_initializer_slot_count_impl(
                program, array_type->element_type, &element_slots) ||
'''
if new_array not in ast:
    if ast.count(old_array) != 1:
        raise SystemExit("array initializer-slot cardinality anchor not found uniquely")
    ast = ast.replace(old_array, new_array, 1)
old_record = '''            field = &record->fields[field_index];
            if (field->element_count == 0U || field->is_flexible_array ||
                !minic_c0_type_initializer_slot_count_impl(program, field->type, &element_slots) ||
'''
new_record = '''            field = &record->fields[field_index];
            if (field->is_flexible_array || field->is_zero_length_array) {
                continue;
            }
            if (field->element_count == 0U ||
                !minic_c0_type_initializer_slot_count_impl(program, field->type, &element_slots) ||
'''
if new_record not in ast:
    if ast.count(old_record) != 1:
        raise SystemExit("record initializer-slot cardinality anchor not found uniquely")
    ast = ast.replace(old_record, new_record, 1)
ast_path.write_text(ast)

# Field-to-slot mapping must use the same zero-slot rule, otherwise a designator
# after a zero-length field points seven (or however many element slots) too far.
global_path = Path("src/frontend/ast_global.c")
global_text = global_path.read_text()
old_field_slot = '''        field = &record->fields[index];
        if (field->element_count == 0U || field->is_flexible_array ||
            !minic_c0_type_initializer_slot_count(program, field->type, &element_slots) ||
'''
new_field_slot = '''        field = &record->fields[index];
        if (field->is_flexible_array || field->is_zero_length_array) {
            continue;
        }
        if (field->element_count == 0U ||
            !minic_c0_type_initializer_slot_count(program, field->type, &element_slots) ||
'''
if new_field_slot not in global_text:
    if global_text.count(old_field_slot) != 1:
        raise SystemExit("global record field slot anchor not found uniquely")
    global_text = global_text.replace(old_field_slot, new_field_slot, 1)
global_path.write_text(global_text)

# Default static record zero-fill is the producer of initializer slots. It must
# not materialize an element for a GNU [0] field.
parser_path = Path("src/frontend/parser_global.c")
parser = parser_path.read_text()
old_zero_fill = '''    /* A flexible array member participates in the record type and alignment,
     * but contributes no scalar initializer slot to the fixed object extent. */
    if (field->is_flexible_array) {
        return true;
    }
'''
new_zero_fill = '''    /* Flexible and GNU zero-length array members participate in record layout,
     * but contribute no scalar initializer slot to the fixed object extent. */
    if (field->is_flexible_array || field->is_zero_length_array) {
        return true;
    }
'''
if new_zero_fill not in parser:
    if parser.count(old_zero_fill) != 1:
        raise SystemExit("static record zero-fill anchor not found uniquely")
    parser = parser.replace(old_zero_fill, new_zero_fill, 1)
parser_path.write_text(parser)
