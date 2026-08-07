#include "frontend/parser_internal.h"

#include <string.h>

static bool
record_has_field(const MinicParser *parser, const MinicRecord *record, MinicSourceSpan name_span) {
    size_t name_length;
    size_t index;

    name_length = minic_parser_span_length(name_span);
    for (index = 0U; index < record->field_count; ++index) {
        const MinicRecordField *field;

        field = minic_c0_record_field(record, index);
        if (field != NULL && field->name_length == name_length &&
            memcmp(field->name, parser->source + name_span.begin.offset, name_length) == 0) {
            return true;
        }
    }
    return false;
}

static bool parse_function_pointer_parameters(MinicParser *parser,
                                              MinicType *parameter_types,
                                              size_t *parameter_count) {
    if (parser->current.kind == MINIC_TOKEN_RPAREN) {
        return true;
    }

    for (;;) {
        MinicType parameter_type;

        if (*parameter_count >= 8U) {
            minic_parser_error(parser, "at most eight function pointer parameters are supported");
            return false;
        }
        if (!minic_parser_parse_type_name(parser, &parameter_type)) {
            return false;
        }
        if (minic_type_is_void(parameter_type)) {
            if (*parameter_count == 0U && parser->current.kind == MINIC_TOKEN_RPAREN) {
                return true;
            }
            minic_parser_error(parser, "function pointer parameter type cannot be bare void");
            return false;
        }

        parameter_types[*parameter_count] = parameter_type;
        *parameter_count += 1U;
        if (parser->current.kind == MINIC_TOKEN_IDENTIFIER && !minic_parser_advance(parser)) {
            return false;
        }
        if (parser->current.kind != MINIC_TOKEN_COMMA) {
            return true;
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
}

static bool parse_function_pointer_field_declarator(
    MinicParser *parser,
    MinicType return_type,
    MinicSourceSpan *name_span,
    MinicType *field_type) {
    MinicType parameter_types[8];
    MinicType function_type;
    size_t parameter_count;

    if (name_span == NULL || field_type == NULL) {
        minic_parser_error(parser, "internal error: missing function pointer declarator output");
        return false;
    }
    parameter_count = 0U;
    (void)memset(parameter_types, 0, sizeof(parameter_types));

    if (!minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '('") ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_STAR, "expected '*' in function pointer declarator")) {
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected function pointer field name");
        return false;
    }
    *name_span = parser->current.span;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_RPAREN, "expected ')' after function pointer name") ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_LPAREN, "expected function pointer parameter list") ||
        !parse_function_pointer_parameters(parser, parameter_types, &parameter_count) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_RPAREN, "expected ')' after function pointer parameters")) {
        return false;
    }
    if (!minic_c0_program_intern_function_type(
            parser->program, return_type, parameter_types, parameter_count, &function_type) ||
        !minic_type_pointer_to(function_type, field_type)) {
        minic_parser_error(parser, "function pointer signature capacity exceeded");
        return false;
    }
    return true;
}

static bool parse_record_field(MinicParser *parser, MinicRecordId record_id) {
    MinicSourceSpan name_span;
    MinicType base_type;
    MinicType field_type;
    size_t element_count;
    const MinicRecord *record;
    bool is_function_pointer;

    if (!minic_parser_parse_type_specifiers(parser, &base_type) ||
        !minic_parser_parse_pointer_declarator(parser, base_type, &field_type)) {
        return false;
    }

    is_function_pointer = parser->current.kind == MINIC_TOKEN_LPAREN;
    if (is_function_pointer) {
        if (!parse_function_pointer_field_declarator(parser, field_type, &name_span, &field_type)) {
            return false;
        }
    } else {
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
        if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
            minic_parser_error(parser, "expected record field name");
            return false;
        }
        name_span = parser->current.span;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }

    record = minic_c0_program_record(parser->program, record_id);
    if (record == NULL) {
        minic_parser_error(parser, "invalid record while adding field");
        return false;
    }
    if (record_has_field(parser, record, name_span)) {
        minic_parser_error(parser, "duplicate record field");
        return false;
    }

    element_count = 1U;
    if (!is_function_pointer && parser->current.kind == MINIC_TOKEN_LBRACKET) {
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_fixed_array_bound(parser, &element_count)) {
            return false;
        }
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after record field")) {
        return false;
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
    return true;
}

bool minic_parser_parse_record_definition_specifier(MinicParser *parser, MinicType *record_type) {
    MinicSourceSpan name_span;
    MinicRecordId record_id;

    if (record_type == NULL) {
        minic_parser_error(parser, "internal error: missing record type output");
        return false;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_KW_STRUCT, "expected keyword 'struct'")) {
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
    if (!minic_c0_program_add_record(parser->program,
                                     parser->source + name_span.begin.offset,
                                     minic_parser_span_length(name_span),
                                     &record_id)) {
        minic_parser_error(parser, "out of memory while adding record");
        return false;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LBRACE, "expected '{' after record tag")) {
        return false;
    }

    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        if (parser->current.kind == MINIC_TOKEN_EOF) {
            minic_parser_error(parser, "expected '}' before end of record");
            return false;
        }
        if (!parse_record_field(parser, record_id)) {
            return false;
        }
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}' after record fields")) {
        return false;
    }
    if (!minic_c0_program_finish_record(parser->program, record_id)) {
        minic_parser_error(parser, "record definition requires at least one field");
        return false;
    }
    *record_type = minic_type_record(record_id);
    return true;
}

bool minic_parser_parse_record_definition(MinicParser *parser) {
    MinicType record_type;

    return minic_parser_parse_record_definition_specifier(parser, &record_type) &&
           minic_parser_expect(
               parser, MINIC_TOKEN_SEMICOLON, "expected ';' after record definition");
}
