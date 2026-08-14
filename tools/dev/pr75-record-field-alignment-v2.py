#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one replacement, found {count}: {old[:140]!r}")
    target.write_text(text.replace(old, new, 1))


def replace_in_function(path: str, signature: str, old: str, new: str) -> None:
    target = Path(path)
    text = target.read_text()
    start = text.find(signature)
    if start < 0:
        raise SystemExit(f"{path}: missing function {signature}")
    next_static = text.find("\nstatic ", start + len(signature))
    next_public = text.find("\nbool ", start + len(signature))
    ends = [value for value in (next_static, next_public) if value >= 0]
    end = min(ends) if ends else len(text)
    body = text[start:end]
    count = body.count(old)
    if count != 1:
        raise SystemExit(f"{path}:{signature}: expected one replacement, found {count}: {old[:120]!r}")
    body = body.replace(old, new, 1)
    target.write_text(text[:start] + body + text[end:])


replace_once(
    "src/frontend/ast.h",
    """    size_t element_count;
    size_t storage_offset;
    bool is_array;
    bool is_flexible_array;
} MinicRecordField;
""",
    """    size_t element_count;
    size_t storage_offset;
    size_t explicit_alignment;
    bool is_array;
    bool is_flexible_array;
} MinicRecordField;
""",
)

path = Path("src/frontend/parser_record.c")
text = path.read_text()
marker = "static bool parse_record_field_declarator(MinicParser *parser,\n"
helper = r'''static bool parse_record_field_alignment_attribute(MinicParser *parser,
                                                  size_t *alignment) {
    int64_t value;

    if (parser == NULL || alignment == NULL) {
        return false;
    }
    *alignment = 0U;
    if (!token_text_equals(parser, parser->current, "__attribute__")) {
        return true;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after field __attribute__") ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '((' in field __attribute__")) {
        return false;
    }
    if (!token_text_equals(parser, parser->current, "__aligned__") &&
        !token_text_equals(parser, parser->current, "aligned")) {
        minic_parser_error(parser, "unsupported GNU record field attribute");
        return false;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after aligned") ||
        !minic_parser_parse_integer_value64(parser, &value) || value <= 0 ||
        (uint64_t)value > (uint64_t)SIZE_MAX ||
        (((uint64_t)value & ((uint64_t)value - 1U)) != 0U) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after aligned value") ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' in field attribute") ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected second ')' in field attribute")) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "record field alignment must be a positive power of two");
        }
        return false;
    }
    *alignment = (size_t)value;
    return true;
}

'''
if text.count(marker) != 1:
    raise SystemExit(f"record-field helper marker: expected 1 match, found {text.count(marker)}")
path.write_text(text.replace(marker, helper + marker, 1))

replace_once(
    "src/frontend/parser_record.c",
    """    size_t element_count;
    MinicRecord *mutable_record;
""",
    """    size_t element_count;
    size_t explicit_alignment;
    MinicRecord *mutable_record;
""",
)
replace_once(
    "src/frontend/parser_record.c",
    """    element_count = 1U;
    is_array = false;
    is_flexible_array = false;
""",
    """    element_count = 1U;
    explicit_alignment = 0U;
    is_array = false;
    is_flexible_array = false;
""",
)
replace_once(
    "src/frontend/parser_record.c",
    """    if (!minic_c0_record_add_field(parser->program,
                                   record_id,
""",
    """    if (!parse_record_field_alignment_attribute(parser, &explicit_alignment)) {
        return false;
    }

    if (!minic_c0_record_add_field(parser->program,
                                   record_id,
""",
)
replace_once(
    "src/frontend/parser_record.c",
    """    mutable_record = &parser->program->records[record_id];
    mutable_record->fields[mutable_record->field_count - 1U].is_array = is_array;
""",
    """    mutable_record = &parser->program->records[record_id];
    mutable_record->fields[mutable_record->field_count - 1U].explicit_alignment = explicit_alignment;
    mutable_record->fields[mutable_record->field_count - 1U].is_array = is_array;
""",
)

replace_once(
    "src/target/riscv64/layout.c",
    """        field_size = field->is_flexible_array ? 0U : element_size * field->element_count;
        if (record->is_union) {
""",
    """        field_size = field->is_flexible_array ? 0U : element_size * field->element_count;
        if (field->explicit_alignment != 0U) {
            if ((field->explicit_alignment & (field->explicit_alignment - 1U)) != 0U) {
                return false;
            }
            if (field->explicit_alignment > field_alignment) {
                field_alignment = field->explicit_alignment;
            }
        }
        if (record->is_union) {
""",
)
replace_once(
    "src/target/riscv64/layout.c",
    """            if (record->is_packed) {
                field_offset = storage_size;
            } else if (!minic_riscv64_align_up(storage_size, field_alignment, &field_offset)) {
""",
    """            if (record->is_packed && field->explicit_alignment == 0U) {
                field_offset = storage_size;
            } else if (!minic_riscv64_align_up(storage_size, field_alignment, &field_offset)) {
""",
)
replace_once(
    "src/target/riscv64/layout.c",
    """        if (!record->is_packed && field_alignment > record_alignment) {
            record_alignment = field_alignment;
        }
""",
    """        if ((!record->is_packed || field->explicit_alignment != 0U) &&
            field_alignment > record_alignment) {
            record_alignment = field_alignment;
        }
""",
)

# Keep parse-time target layout and __builtin_offsetof consistent with final RV64 layout.
alignment_logic = """if (field->explicit_alignment != 0U) {
            if ((field->explicit_alignment & (field->explicit_alignment - 1U)) != 0U) {
                return false;
            }
            if ((uint64_t)field->explicit_alignment > field_alignment) {
                field_alignment = (uint64_t)field->explicit_alignment;
            }
        }
"""
replace_in_function(
    "src/frontend/parser_core.c",
    "static bool constant_type_layout(",
    """field_size = field->is_flexible_array ? 0U : element_size * field->element_count;
            if (record->is_union) {
""",
    """field_size = field->is_flexible_array ? 0U : element_size * field->element_count;
            if (field->explicit_alignment != 0U) {
                if ((field->explicit_alignment & (field->explicit_alignment - 1U)) != 0U) {
                    return false;
                }
                if ((uint64_t)field->explicit_alignment > field_alignment) {
                    field_alignment = (uint64_t)field->explicit_alignment;
                }
            }
            if (record->is_union) {
""",
)
replace_in_function(
    "src/frontend/parser_core.c",
    "static bool constant_type_layout(",
    """if (record->is_packed) {
                    field_offset = storage_size;
                } else if (!constant_align_up(storage_size, field_alignment, &field_offset)) {
""",
    """if (record->is_packed && field->explicit_alignment == 0U) {
                    field_offset = storage_size;
                } else if (!constant_align_up(storage_size, field_alignment, &field_offset)) {
""",
)
replace_in_function(
    "src/frontend/parser_core.c",
    "static bool constant_type_layout(",
    """if (!record->is_packed && field_alignment > record_alignment) {
                record_alignment = field_alignment;
            }
""",
    """if ((!record->is_packed || field->explicit_alignment != 0U) &&
                field_alignment > record_alignment) {
                record_alignment = field_alignment;
            }
""",
)
replace_in_function(
    "src/frontend/parser_core.c",
    "static bool constant_record_member_offset(",
    """field_size = field->is_flexible_array ? 0U : element_size * field->element_count;
        if (record->is_union) {
""",
    """field_size = field->is_flexible_array ? 0U : element_size * field->element_count;
        if (field->explicit_alignment != 0U) {
            if ((field->explicit_alignment & (field->explicit_alignment - 1U)) != 0U) {
                return false;
            }
            if ((uint64_t)field->explicit_alignment > field_alignment) {
                field_alignment = (uint64_t)field->explicit_alignment;
            }
        }
        if (record->is_union) {
""",
)
replace_in_function(
    "src/frontend/parser_core.c",
    "static bool constant_record_member_offset(",
    """} else if (record->is_packed) {
            field_offset = storage_size;
        } else if (!constant_align_up(storage_size, field_alignment, &field_offset)) {
""",
    """} else if (record->is_packed && field->explicit_alignment == 0U) {
            field_offset = storage_size;
        } else if (!constant_align_up(storage_size, field_alignment, &field_offset)) {
""",
)

print("staged GNU aligned record fields with robust RV64 layout/offsetof patching")
