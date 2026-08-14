#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_record.c")
text = path.read_text()
start = text.find("static bool parse_record_field(MinicParser *parser, MinicRecordId record_id) {")
end = text.find("\nbool minic_parser_parse_record_definition_specifier", start)
if start < 0 or end < 0:
    raise SystemExit("cannot locate parse_record_field")
replacement = r'''static bool parse_record_field_declarator(MinicParser *parser,
                                          MinicRecordId record_id,
                                          MinicType base_type) {
    MinicSourceSpan name_span;
    MinicType field_type;
    size_t element_count;
    MinicRecord *mutable_record;
    const MinicRecord *record;
    bool is_flexible_array;

    record = minic_c0_program_record(parser->program, record_id);
    if (record == NULL) {
        minic_parser_error(parser, "invalid record while adding field");
        return false;
    }
    if (!minic_parser_parse_pointer_declarator(parser, base_type, &field_type)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        if (!parse_function_pointer_field_declarator(parser, field_type, &name_span, &field_type)) {
            return false;
        }
    } else {
        if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
            minic_parser_error(parser, "expected record field name");
            return false;
        }
        name_span = parser->current.span;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }

    if (minic_type_is_void(field_type)) {
        minic_parser_error(parser, "record field cannot have void type");
        return false;
    }
    if (minic_type_is_array(field_type)) {
        minic_parser_error(parser, "record field typedef array is unsupported");
        return false;
    }
    if (!minic_parser_require_complete_object_type(
            parser, field_type, "record field cannot use incomplete type by value")) {
        return false;
    }
    if (record_has_field(parser, record, name_span)) {
        minic_parser_error(parser, "duplicate record field");
        return false;
    }

    element_count = 1U;
    is_flexible_array = false;
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        if (minic_type_is_pointer(field_type) && field_type.base_kind == MINIC_TYPE_BASE_FUNCTION) {
            minic_parser_error(parser, "function pointer field arrays are unsupported");
            return false;
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_RBRACKET) {
            if (record->is_union) {
                minic_parser_error(parser, "flexible array member is not allowed in union");
                return false;
            }
            if (record->field_count == 0U) {
                minic_parser_error(parser,
                                   "flexible array member requires a preceding named field");
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

    if (!minic_c0_record_add_field(parser->program,
                                   record_id,
                                   parser->source + name_span.begin.offset,
                                   minic_parser_span_length(name_span),
                                   field_type,
                                   element_count)) {
        minic_parser_error(parser, "out of memory while adding record field");
        return false;
    }
    if (is_flexible_array) {
        mutable_record = &parser->program->records[record_id];
        mutable_record->fields[mutable_record->field_count - 1U].is_flexible_array = true;
    }
    return true;
}

static bool parse_record_field(MinicParser *parser, MinicRecordId record_id) {
    MinicType base_type;
    const MinicRecord *record;

    record = minic_c0_program_record(parser->program, record_id);
    if (record == NULL) {
        minic_parser_error(parser, "invalid record while adding field");
        return false;
    }
    if (record->field_count > 0U && record->fields[record->field_count - 1U].is_flexible_array) {
        minic_parser_error(parser, "flexible array member must be the last record field");
        return false;
    }
    if (!minic_parser_parse_type_specifiers(parser, &base_type)) {
        return false;
    }

    for (;;) {
        if (!parse_record_field_declarator(parser, record_id, base_type)) {
            return false;
        }
        record = minic_c0_program_record(parser->program, record_id);
        if (record == NULL || record->field_count == 0U) {
            minic_parser_error(parser, "invalid record after adding field");
            return false;
        }
        if (parser->current.kind != MINIC_TOKEN_COMMA) {
            return minic_parser_expect(
                parser, MINIC_TOKEN_SEMICOLON, "expected ';' after record field");
        }
        if (record->fields[record->field_count - 1U].is_flexible_array) {
            minic_parser_error(parser, "flexible array member must be the last record field");
            return false;
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
}
'''
path.write_text(text[:start] + replacement + text[end:])
print("staged comma-separated record field declarators")
