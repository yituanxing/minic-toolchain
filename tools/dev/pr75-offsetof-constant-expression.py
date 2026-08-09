#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_core.c")
text = path.read_text()
marker = "static bool parse_array_bound_primary(MinicParser *parser, int64_t *value) {\n"
start = text.find(marker)
if start < 0 or text.find(marker, start + 1) >= 0:
    raise SystemExit("cannot uniquely locate integer constant-expression primary parser")

helper = r'''static bool current_is_builtin_offsetof_constant(const MinicParser *parser) {
    static const char name[] = "__builtin_offsetof";
    size_t length;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        return false;
    }
    length = minic_parser_span_length(parser->current.span);
    return length == sizeof(name) - 1U &&
           memcmp(parser->source + parser->current.span.begin.offset, name, length) == 0;
}

static bool parse_offsetof_integer_constant(MinicParser *parser, int64_t *value) {
    MinicSourceSpan field_span;
    MinicType record_type;
    const MinicRecord *record;
    size_t field_index;
    size_t field_name_length;

    if (parser == NULL || value == NULL || !current_is_builtin_offsetof_constant(parser) ||
        !minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after __builtin_offsetof") ||
        !minic_parser_parse_type_name(parser, &record_type)) {
        return false;
    }
    if (!minic_type_is_record(record_type)) {
        minic_parser_error(parser, "__builtin_offsetof requires a record type");
        return false;
    }
    record = minic_c0_program_record(parser->program, record_type.record_id);
    if (record == NULL || !record->is_complete) {
        minic_parser_error(parser, "__builtin_offsetof requires a complete record type");
        return false;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_COMMA, "expected ',' in __builtin_offsetof") ||
        parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "expected record field in __builtin_offsetof");
        }
        return false;
    }

    field_span = parser->current.span;
    field_name_length = minic_parser_span_length(field_span);
    field_index = 0U;
    while (field_index < record->field_count) {
        const MinicRecordField *field;

        field = minic_c0_record_field(record, field_index);
        if (field != NULL && field->name_length == field_name_length &&
            memcmp(field->name,
                   parser->source + field_span.begin.offset,
                   field_name_length) == 0) {
            break;
        }
        field_index += 1U;
    }
    if (field_index == record->field_count) {
        minic_parser_error(parser, "record has no such field in __builtin_offsetof");
        return false;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after __builtin_offsetof")) {
        return false;
    }
    if (record->fields[field_index].storage_offset > (size_t)INT64_MAX) {
        minic_parser_error(parser, "__builtin_offsetof result is too large for integer constant expression");
        return false;
    }
    *value = (int64_t)record->fields[field_index].storage_offset;
    return true;
}

'''
text = text[:start] + helper + text[start:]

primary_start = text.index(marker, start + len(helper))
needle = '''    if (parser == NULL || value == NULL) {
        return false;
    }
'''
position = text.find(needle, primary_start)
if position < 0:
    raise SystemExit("cannot locate constant-expression primary prologue")
insert_at = position + len(needle)
text = (
    text[:insert_at]
    + '''    if (current_is_builtin_offsetof_constant(parser)) {
        return parse_offsetof_integer_constant(parser, value);
    }
'''
    + text[insert_at:]
)
path.write_text(text)
print("staged __builtin_offsetof in shared integer constant expressions")
