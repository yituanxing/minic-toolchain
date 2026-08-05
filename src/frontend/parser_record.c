#include "frontend/parser_internal.h"

#include <string.h>

static bool record_has_field(
    const MinicParser *parser,
    const MinicRecord *record,
    MinicSourceSpan name_span)
{
    size_t name_length;
    size_t index;

    name_length = minic_parser_span_length(name_span);
    for (index = 0U; index < record->field_count; ++index) {
        const MinicRecordField *field;

        field = minic_c0_record_field(record, index);
        if (field != NULL && field->name_length == name_length &&
            memcmp(
                field->name,
                parser->source + name_span.begin.offset,
                name_length) == 0) {
            return true;
        }
    }
    return false;
}

static bool parse_record_field(
    MinicParser *parser,
    MinicRecordId record_id)
{
    MinicSourceSpan name_span;
    MinicType field_type;
    size_t element_count;
    const MinicRecord *record;

    if (!minic_parser_expect(
            parser,
            MINIC_TOKEN_KW_INT,
            "record field type must start with 'int'")) {
        return false;
    }
    field_type = minic_type_int();
    while (parser->current.kind == MINIC_TOKEN_STAR) {
        if (!minic_type_pointer_to(field_type, &field_type) ||
            !minic_parser_advance(parser)) {
            minic_parser_error(parser, "record field pointer depth is unsupported");
            return false;
        }
    }
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected record field name");
        return false;
    }

    name_span = parser->current.span;
    record = minic_c0_program_record(parser->program, record_id);
    if (record == NULL) {
        minic_parser_error(parser, "invalid record while adding field");
        return false;
    }
    if (record_has_field(parser, record, name_span)) {
        minic_parser_error(parser, "duplicate record field");
        return false;
    }
    if (!minic_parser_advance(parser)) {
        return false;
    }

    element_count = 1U;
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_fixed_array_bound(parser, &element_count)) {
            return false;
        }
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after record field")) {
        return false;
    }
    if (!minic_c0_record_add_field(
            parser->program,
            record_id,
            parser->source + name_span.begin.offset,
            minic_parser_span_length(name_span),
            field_type,
            element_count)) {
        minic_parser_error(parser, "out of memory while adding record field");
        return false;
    }
    return true;
}

bool minic_parser_parse_record_definition(MinicParser *parser)
{
    MinicSourceSpan name_span;
    MinicRecordId record_id;

    if (!minic_parser_expect(
            parser,
            MINIC_TOKEN_KW_STRUCT,
            "expected keyword 'struct'")) {
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected record tag after 'struct'");
        return false;
    }

    name_span = parser->current.span;
    if (minic_parser_find_record(parser, name_span) != MINIC_RECORD_INVALID) {
        minic_parser_error(parser, "duplicate record definition");
        return false;
    }
    if (!minic_c0_program_add_record(
            parser->program,
            parser->source + name_span.begin.offset,
            minic_parser_span_length(name_span),
            &record_id)) {
        minic_parser_error(parser, "out of memory while adding record");
        return false;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(
            parser,
            MINIC_TOKEN_LBRACE,
            "expected '{' after record tag")) {
        return false;
    }

    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        if (parser->current.kind == MINIC_TOKEN_EOF) {
            minic_parser_error(parser, "expected '}' before end of record");
            return false;
        }
        if (parser->current.kind != MINIC_TOKEN_KW_INT) {
            minic_parser_error(parser, "record field type must start with 'int'");
            return false;
        }
        if (!parse_record_field(parser, record_id)) {
            return false;
        }
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}' after record fields") ||
        !minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after record definition")) {
        return false;
    }
    if (!minic_c0_program_finish_record(parser->program, record_id)) {
        minic_parser_error(parser, "record definition requires at least one field");
        return false;
    }
    return true;
}
