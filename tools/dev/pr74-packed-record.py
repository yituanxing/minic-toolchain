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
    """    size_t storage_size;
    size_t alignment;
    bool is_union;
    bool is_complete;
""",
    """    size_t storage_size;
    size_t alignment;
    bool is_union;
    bool is_packed;
    bool is_complete;
""",
)

replace_once(
    "src/frontend/parser_record.c",
    """static bool parse_record_field(MinicParser *parser, MinicRecordId record_id) {
""",
    """static bool token_text_equals(const MinicParser *parser,
                              MinicToken token,
                              const char *text) {
    size_t length;

    if (parser == NULL || text == NULL || token.kind != MINIC_TOKEN_IDENTIFIER) {
        return false;
    }
    length = minic_parser_span_length(token.span);
    return strlen(text) == length &&
           memcmp(parser->source + token.span.begin.offset, text, length) == 0;
}

static bool parse_packed_record_attribute(MinicParser *parser, bool *is_packed) {
    if (parser == NULL || is_packed == NULL) {
        return false;
    }
    *is_packed = false;
    if (!token_text_equals(parser, parser->current, "__attribute__")) {
        return true;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after __attribute__") ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' in __attribute__")) {
        return false;
    }
    if (!token_text_equals(parser, parser->current, "__packed__") &&
        !token_text_equals(parser, parser->current, "packed")) {
        minic_parser_error(parser, "only packed record attribute is supported here");
        return false;
    }
    *is_packed = true;
    return minic_parser_advance(parser) &&
           minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after packed attribute") &&
           minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after __attribute__");
}

static bool parse_record_field(MinicParser *parser, MinicRecordId record_id) {
""",
)

replace_once(
    "src/frontend/parser_record.c",
    """    MinicTokenKind record_keyword;
    bool is_union;
""",
    """    MinicTokenKind record_keyword;
    bool is_packed;
    bool is_union;
""",
)

replace_once(
    "src/frontend/parser_record.c",
    """    is_union = record_keyword == MINIC_TOKEN_KW_UNION;
    if (!minic_parser_advance(parser)) {
        return false;
    }

    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
""",
    """    is_union = record_keyword == MINIC_TOKEN_KW_UNION;
    if (!minic_parser_advance(parser) || !parse_packed_record_attribute(parser, &is_packed)) {
        return false;
    }

    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
""",
)

replace_once(
    "src/frontend/parser_record.c",
    """            parser->program->records[record_id].is_union = is_union;
        } else {
            record = minic_c0_program_record(parser->program, record_id);
            if (record == NULL || record->is_complete || record->is_union != is_union) {
""",
    """            parser->program->records[record_id].is_union = is_union;
            parser->program->records[record_id].is_packed = is_packed;
        } else {
            record = minic_c0_program_record(parser->program, record_id);
            if (record == NULL || record->is_complete || record->is_union != is_union ||
                (is_packed && record->is_packed != is_packed)) {
""",
)

replace_once(
    "src/frontend/parser_record.c",
    """        parser->program->records[record_id].is_union = is_union;
    } else {
""",
    """        parser->program->records[record_id].is_union = is_union;
        parser->program->records[record_id].is_packed = is_packed;
    } else {
""",
)

replace_once(
    "src/target/riscv64/layout.c",
    """        if (record->is_union) {
            field_offset = 0U;
            if (field_size > storage_size) {
                storage_size = field_size;
            }
        } else {
            if (!minic_riscv64_align_up(storage_size, field_alignment, &field_offset) ||
                field_offset > SIZE_MAX - field_size) {
                return false;
            }
            storage_size = field_offset + field_size;
        }
        field->storage_offset = field_offset;
        if (field_alignment > record_alignment) {
            record_alignment = field_alignment;
        }
""",
    """        if (record->is_union) {
            field_offset = 0U;
            if (field_size > storage_size) {
                storage_size = field_size;
            }
        } else {
            if (record->is_packed) {
                field_offset = storage_size;
            } else if (!minic_riscv64_align_up(storage_size, field_alignment, &field_offset)) {
                return false;
            }
            if (field_offset > SIZE_MAX - field_size) {
                return false;
            }
            storage_size = field_offset + field_size;
        }
        field->storage_offset = field_offset;
        if (!record->is_packed && field_alignment > record_alignment) {
            record_alignment = field_alignment;
        }
""",
)

print("staged GNU packed record parsing and RV64 layout")
