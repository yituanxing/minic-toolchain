#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:120]!r}")
    target.write_text(text.replace(old, new, 1))


replace_once(
    "src/frontend/ast.h",
    """    MinicType type;
    size_t element_count;
    size_t storage_offset;
} MinicRecordField;
""",
    """    MinicType type;
    size_t element_count;
    size_t storage_offset;
    bool is_flexible_array;
} MinicRecordField;
""",
)

replace_once(
    "src/frontend/parser_record.c",
    """static bool parse_record_field(MinicParser *parser, MinicRecordId record_id) {
    MinicSourceSpan name_span;
    MinicType base_type;
    MinicType field_type;
    size_t element_count;
    const MinicRecord *record;
""",
    """static bool parse_record_field(MinicParser *parser, MinicRecordId record_id) {
    MinicSourceSpan name_span;
    MinicType base_type;
    MinicType field_type;
    size_t element_count;
    MinicRecord *mutable_record;
    const MinicRecord *record;
    bool is_flexible_array;
""",
)

replace_once(
    "src/frontend/parser_record.c",
    """    if (!minic_parser_parse_type_specifiers(parser, &base_type) ||
        !minic_parser_parse_pointer_declarator(parser, base_type, &field_type)) {
        return false;
    }
""",
    """    record = minic_c0_program_record(parser->program, record_id);
    if (record == NULL) {
        minic_parser_error(parser, \"invalid record while adding field\");
        return false;
    }
    if (record->field_count > 0U && record->fields[record->field_count - 1U].is_flexible_array) {
        minic_parser_error(parser, \"flexible array member must be the last record field\");
        return false;
    }
    if (!minic_parser_parse_type_specifiers(parser, &base_type) ||
        !minic_parser_parse_pointer_declarator(parser, base_type, &field_type)) {
        return false;
    }
""",
)

replace_once(
    "src/frontend/parser_record.c",
    """    record = minic_c0_program_record(parser->program, record_id);
    if (record == NULL) {
        minic_parser_error(parser, \"invalid record while adding field\");
        return false;
    }
    if (record_has_field(parser, record, name_span)) {
""",
    """    if (record_has_field(parser, record, name_span)) {
""",
)

replace_once(
    "src/frontend/parser_record.c",
    """    element_count = 1U;
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        if (minic_type_is_pointer(field_type) && field_type.base_kind == MINIC_TYPE_BASE_FUNCTION) {
            minic_parser_error(parser, \"function pointer field arrays are unsupported\");
            return false;
        }
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_fixed_array_bound(parser, &element_count)) {
            return false;
        }
    }
""",
    """    element_count = 1U;
    is_flexible_array = false;
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        if (minic_type_is_pointer(field_type) && field_type.base_kind == MINIC_TYPE_BASE_FUNCTION) {
            minic_parser_error(parser, \"function pointer field arrays are unsupported\");
            return false;
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_RBRACKET) {
            if (record->is_union) {
                minic_parser_error(parser, \"flexible array member is not allowed in union\");
                return false;
            }
            if (record->field_count == 0U) {
                minic_parser_error(parser, \"flexible array member requires a preceding named field\");
                return false;
            }
            is_flexible_array = true;
            if (!minic_parser_advance(parser)) {
                return false;
            }
        } else if (!minic_parser_parse_fixed_array_bound(parser, &element_count)) {
            return false;
        }
    }
""",
)

replace_once(
    "src/frontend/parser_record.c",
    """        minic_parser_error(parser, \"out of memory while adding record field\");
        return false;
    }
    return true;
}
""",
    """        minic_parser_error(parser, \"out of memory while adding record field\");
        return false;
    }
    if (is_flexible_array) {
        mutable_record = &parser->program->records[record_id];
        mutable_record->fields[mutable_record->field_count - 1U].is_flexible_array = true;
    }
    return true;
}
""",
)

replace_once(
    "src/frontend/parser_member.c",
    """    if (field->element_count > 1U) {
""",
    """    if (field->is_flexible_array || field->element_count > 1U) {
""",
)

replace_once(
    "src/target/riscv64/layout.c",
    """        if (element_size > SIZE_MAX / field->element_count) {
            return false;
        }
        field_size = element_size * field->element_count;
""",
    """        if (element_size > SIZE_MAX / field->element_count) {
            return false;
        }
        field_size = field->is_flexible_array ? 0U : element_size * field->element_count;
""",
)

print("staged C99 flexible array member parsing, access, and RV64 layout")
