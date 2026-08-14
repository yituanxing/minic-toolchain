#!/usr/bin/env python3
from pathlib import Path
import re


def read(path: str) -> str:
    return Path(path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    Path(path).write_text(text.rstrip() + "\n", encoding="utf-8")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


def function_span(text: str, name: str) -> tuple[int, int]:
    marker = name + "("
    marker_index = text.index(marker)
    start = text.rfind("\n", 0, marker_index) + 1
    brace = text.index("{", marker_index)
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return start, index + 1
    raise SystemExit(f"unterminated function {name}")


# --- RV64 global/static emitter: replace all record-field cache reads with DataLayout queries. ---
path = "src/target/riscv64/codegen_function.c"
text = read(path)
old = """        if (field == NULL || field->element_count != 1U || field->is_flexible_array ||
            !minic_riscv64_type_layout(program, field->type, &field_size, &field_alignment)) {
            return false;
        }
        (void)field_alignment;
        field_offset = field->storage_offset;
"""
new = """        if (field == NULL || field->element_count != 1U || field->is_flexible_array ||
            !minic_riscv64_type_layout(program, field->type, &field_size, &field_alignment) ||
            !minic_data_layout_record_field_offset(minic_default_data_layout(),
                                                   program,
                                                   record,
                                                   field_index,
                                                   &field_offset)) {
            return false;
        }
        (void)field_alignment;
"""
text = replace_once(text, old, new, "direct record values field offset")

old = """            if (field == NULL || field->element_count == 0U || field->is_flexible_array) {
                return false;
            }
            field_offset = record->is_union ? 0U : field->storage_offset;
"""
new = """            if (field == NULL || field->element_count == 0U || field->is_flexible_array) {
                return false;
            }
            if (record->is_union) {
                field_offset = 0U;
            } else if (!minic_data_layout_record_field_offset(minic_default_data_layout(),
                                                              program,
                                                              record,
                                                              field_index,
                                                              &field_offset)) {
                return false;
            }
"""
text = replace_once(text, old, new, "recursive aggregate field offset")

old = """            if (field == NULL || field->element_count != 1U ||
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
write(path, text)


# --- RV64 expression emitter: make record size/member/bit-field layout query-only. ---
path = "src/target/riscv64/codegen_expression.c"
text = read(path)

# Bit-field byte offset within the storage unit is a DataLayout result. Pass it
# explicitly into the low-level load/store routines instead of reading AST cache state.
for name in ("minic_riscv64_emit_bit_field_load_from_address", "minic_riscv64_emit_bit_field_store_to_address"):
    start, end = function_span(text, name)
    block = text[start:end]
    anchor = "const MinicRecordField *field,\n"
    if anchor not in block:
        raise SystemExit(f"{name}: field parameter anchor missing")
    block = block.replace(anchor, anchor + "                                                           size_t bit_offset,\n", 1)
    block = block.replace("field->bit_offset", "bit_offset")
    text = text[:start] + block + text[end:]

for name, callee in (
    ("minic_riscv64_emit_lvalue_load_from_address", "minic_riscv64_emit_bit_field_load_from_address"),
    ("minic_riscv64_emit_lvalue_store_to_address", "minic_riscv64_emit_bit_field_store_to_address"),
):
    start, end = function_span(text, name)
    block = text[start:end]
    old = """    const MinicRecordField *field;

    field = minic_c0_expression_bit_field(program, expression_id);
    if (field != NULL) {
        return CALLEE(
            file, field, RESULT, address_register);
    }
"""
    # The two helpers have different register parameter names; handle structurally.
    field_anchor = "    const MinicRecordField *field;\n"
    if field_anchor not in block:
        raise SystemExit(f"{name}: field local anchor missing")
    block = block.replace(
        field_anchor,
        field_anchor + "    const MinicExpression *expression;\n    const MinicRecord *record;\n    size_t bit_offset;\n    size_t field_offset;\n",
        1,
    )
    assignment = "    field = minic_c0_expression_bit_field(program, expression_id);\n"
    if assignment not in block:
        raise SystemExit(f"{name}: bit-field lookup anchor missing")
    block = block.replace(
        assignment,
        """    expression = minic_c0_program_expression(program, expression_id);
    field = minic_c0_expression_bit_field(program, expression_id);
    record = expression != NULL && expression->kind == MINIC_EXPRESSION_MEMBER
                 ? minic_c0_program_record(program, expression->value.member.record_id)
                 : NULL;
""",
        1,
    )
    if callee == "minic_riscv64_emit_bit_field_load_from_address":
        old_call = """        return minic_riscv64_emit_bit_field_load_from_address(
            file, field, result_register, address_register);
"""
        new_call = """        if (record == NULL ||
            !minic_data_layout_record_field_layout(minic_default_data_layout(),
                                                   program,
                                                   record,
                                                   expression->value.member.field_index,
                                                   &field_offset,
                                                   &bit_offset)) {
            return false;
        }
        (void)field_offset;
        return minic_riscv64_emit_bit_field_load_from_address(
            file, field, bit_offset, result_register, address_register);
"""
    else:
        old_call = """        return minic_riscv64_emit_bit_field_store_to_address(
            file, field, value_register, address_register);
"""
        new_call = """        if (record == NULL ||
            !minic_data_layout_record_field_layout(minic_default_data_layout(),
                                                   program,
                                                   record,
                                                   expression->value.member.field_index,
                                                   &field_offset,
                                                   &bit_offset)) {
            return false;
        }
        (void)field_offset;
        return minic_riscv64_emit_bit_field_store_to_address(
            file, field, bit_offset, value_register, address_register);
"""
    if old_call not in block:
        raise SystemExit(f"{name}: bit-field call anchor missing")
    block = block.replace(old_call, new_call, 1)
    text = text[:start] + block + text[end:]

# Member address: query field offset from DataLayout.
start, end = function_span(text, "minic_riscv64_emit_member_address")
block = text[start:end]
anchor = "    const MinicRecordField *field;\n"
if anchor not in block:
    raise SystemExit("member address field local anchor missing")
block = block.replace(anchor, anchor + "    size_t field_offset;\n", 1)
old = """    if (base == NULL || record == NULL || field == NULL ||
        !minic_type_pointee(base->type, &record_type) || !minic_type_is_record(record_type) ||
        record_type.record_id != expression->value.member.record_id ||
        !minic_riscv64_emit_expression(
            file, program, function, function_layout, expression->value.member.base)) {
        return false;
    }
"""
new = """    if (base == NULL || record == NULL || field == NULL ||
        !minic_type_pointee(base->type, &record_type) || !minic_type_is_record(record_type) ||
        record_type.record_id != expression->value.member.record_id ||
        !minic_data_layout_record_field_offset(minic_default_data_layout(),
                                               program,
                                               record,
                                               expression->value.member.field_index,
                                               &field_offset) ||
        !minic_riscv64_emit_expression(
            file, program, function, function_layout, expression->value.member.base)) {
        return false;
    }
"""
block = replace_once(block, old, new, "member address DataLayout query")
block = block.replace("field->storage_offset", "field_offset")
text = text[:start] + block + text[end:]

# Record-valued member rvalue: query record size and field offset.
start, end = function_span(text, "minic_riscv64_emit_record_rvalue_member")
block = text[start:end]
anchor = "    size_t storage_size;\n"
if anchor not in block:
    raise SystemExit("record rvalue member storage local anchor missing")
block = block.replace(anchor, "    size_t field_offset;\n" + anchor, 1)
old = """        field->is_array || minic_type_is_record(field->type) || record->storage_size == 0U ||
        record->storage_size > SIZE_MAX - 15U) {
        return false;
    }
    storage_size = record->storage_size;
"""
new = """        field->is_array || minic_type_is_record(field->type) ||
        !minic_riscv64_type_layout(program,
                                   minic_type_record(expression->value.member.record_id),
                                   &storage_size,
                                   &temporary_size) ||
        storage_size == 0U || storage_size > SIZE_MAX - 15U ||
        !minic_data_layout_record_field_offset(minic_default_data_layout(),
                                               program,
                                               record,
                                               expression->value.member.field_index,
                                               &field_offset)) {
        return false;
    }
"""
block = replace_once(block, old, new, "record rvalue size query")
# temporary_size was used as scratch alignment above; overwrite with aligned temporary next.
block = block.replace("field->storage_offset", "field_offset")
text = text[:start] + block + text[end:]

# Record copy: query record size from DataLayout.
start, end = function_span(text, "minic_riscv64_emit_record_copy_value")
block = text[start:end]
old = """    if (record == NULL || !record->is_complete || record->storage_size == 0U ||
        record->storage_size > SIZE_MAX - 15U) {
        return false;
    }
    storage_size = record->storage_size;
"""
new = """    if (record == NULL || !record->is_complete ||
        !minic_riscv64_type_layout(program, target->type, &storage_size, &temporary_size) ||
        storage_size == 0U || storage_size > SIZE_MAX - 15U) {
        return false;
    }
"""
block = replace_once(block, old, new, "record copy size query")
text = text[:start] + block + text[end:]

# offsetof emission: compute the selected field offset instead of reading the cache.
old = """        if (record == NULL || field == NULL || !record->is_complete ||
            !minic_type_equal(expression->type, minic_type_unsigned_long()) ||
            expression->value.offsetof_value.anonymous_prefix_offset >
                SIZE_MAX - field->storage_offset) {
            return false;
        }
        offset = expression->value.offsetof_value.anonymous_prefix_offset + field->storage_offset;
"""
new = """        if (record == NULL || field == NULL || !record->is_complete ||
            !minic_type_equal(expression->type, minic_type_unsigned_long()) ||
            !minic_data_layout_record_field_offset(minic_default_data_layout(),
                                                   program,
                                                   record,
                                                   expression->value.offsetof_value.field_index,
                                                   &offset) ||
            expression->value.offsetof_value.anonymous_prefix_offset > SIZE_MAX - offset) {
            return false;
        }
        offset = expression->value.offsetof_value.anonymous_prefix_offset + offset;
"""
text = replace_once(text, old, new, "offsetof DataLayout query")
write(path, text)

print("MATERIALIZED record-datalayout-query-v1")
