#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:140]!r}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/ast.h",
    """    size_t element_count;
    size_t storage_offset;
    bool is_flexible_array;
} MinicRecordField;
""",
    """    size_t element_count;
    size_t storage_offset;
    bool is_array;
    bool is_flexible_array;
} MinicRecordField;
""",
)

replace_once(
    "src/frontend/parser_record.c",
    """    const MinicRecord *record;
    bool is_flexible_array;

    record = minic_c0_program_record(parser->program, record_id);
""",
    """    const MinicRecord *record;
    bool is_array;
    bool is_flexible_array;

    record = minic_c0_program_record(parser->program, record_id);
""",
)

replace_once(
    "src/frontend/parser_record.c",
    """    element_count = 1U;
    is_flexible_array = false;
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
""",
    """    element_count = 1U;
    is_array = false;
    is_flexible_array = false;
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        is_array = true;
""",
)

replace_once(
    "src/frontend/parser_record.c",
    """    if (is_flexible_array) {
        mutable_record = &parser->program->records[record_id];
        mutable_record->fields[mutable_record->field_count - 1U].is_flexible_array = true;
    }
    return true;
""",
    """    mutable_record = &parser->program->records[record_id];
    mutable_record->fields[mutable_record->field_count - 1U].is_array = is_array;
    mutable_record->fields[mutable_record->field_count - 1U].is_flexible_array = is_flexible_array;
    return true;
""",
)

replace_once(
    "src/frontend/parser_member.c",
    """    if (field->is_flexible_array || field->element_count > 1U) {
""",
    """    if (field->is_array) {
""",
)

replace_once(
    "src/frontend/ast_verifier.c",
    """        if (field->is_flexible_array || field->element_count > 1U) {
""",
    """        if (field->is_array) {
""",
)

print("staged explicit record-field array identity including length-one arrays")
