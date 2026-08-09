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

static bool parse_function_pointer_field_declarator(MinicParser *parser,
                                                    MinicType return_type,
                                                    MinicSourceSpan *name_span,
                                                    MinicType *field_type) {
    MinicType parameter_types[8];
    MinicType function_type;
    size_t parameter_count;
    size_t pointer_depth;
    bool is_variadic;

    parameter_count = 0U;
    pointer_depth = 0U;
    is_variadic = false;
    (void)memset(parameter_types, 0, sizeof(parameter_types));

    if (!minic_parser_expect(
            parser, MINIC_TOKEN_LPAREN, "expected '(' before function pointer declarator")) {
        return false;
    }
    while (parser->current.kind == MINIC_TOKEN_STAR) {
        pointer_depth += 1U;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    if (pointer_depth == 0U) {
        minic_parser_error(parser, "function pointer declarator requires '*'");
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected function pointer field name");
        return false;
    }
    *name_span = parser->current.span;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_RPAREN, "expected ')' after function pointer field name") ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_LPAREN, "expected '(' before function pointer parameters") ||
        !minic_parser_parse_parameter_list(
            parser, NULL, parameter_types, &parameter_count, false, &is_variadic) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_RPAREN, "expected ')' after function pointer parameters")) {
        return false;
    }
    if (is_variadic) {
        minic_parser_error(parser, "variadic function pointer fields are not supported yet");
        return false;
    }
    if (!minic_c0_program_add_function_type(
            parser->program, return_type, parameter_types, parameter_count, &function_type)) {
        minic_parser_error(parser, "cannot build function pointer field type");
        return false;
    }
    while (pointer_depth > 0U) {
        if (!minic_type_pointer_to(function_type, &function_type)) {
            minic_parser_error(parser, "function pointer declarator depth is unsupported");
            return false;
        }
        pointer_depth -= 1U;
    }
    *field_type = function_type;
    return true;
}

static bool token_text_equals(const MinicParser *parser, MinicToken token, const char *text) {
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
    MinicSourceSpan name_span;
    MinicType base_type;
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
    if (record->field_count > 0U && record->fields[record->field_count - 1U].is_flexible_array) {
        minic_parser_error(parser, "flexible array member must be the last record field");
        return false;
    }
    if (!minic_parser_parse_type_specifiers(parser, &base_type) ||
        !minic_parser_parse_pointer_declarator(parser, base_type, &field_type)) {
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
    if (is_flexible_array) {
        mutable_record = &parser->program->records[record_id];
        mutable_record->fields[mutable_record->field_count - 1U].is_flexible_array = true;
    }
    return true;
}

bool minic_parser_parse_record_definition_specifier(MinicParser *parser, MinicType *record_type) {
    MinicRecordId record_id;
    MinicTokenKind record_keyword;
    bool is_packed;
    bool is_union;

    if (record_type == NULL) {
        minic_parser_error(parser, "internal error: missing record type output");
        return false;
    }
    record_keyword = parser->current.kind;
    if (record_keyword != MINIC_TOKEN_KW_STRUCT && record_keyword != MINIC_TOKEN_KW_UNION) {
        minic_parser_error(parser, "expected record keyword");
        return false;
    }
    is_union = record_keyword == MINIC_TOKEN_KW_UNION;
    if (!minic_parser_advance(parser) || !parse_packed_record_attribute(parser, &is_packed)) {
        return false;
    }

    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        MinicSourceSpan name_span;
        const MinicRecord *record;

        name_span = parser->current.span;
        record_id = minic_parser_find_record(parser, name_span);
        if (record_id == MINIC_RECORD_INVALID) {
            if (!minic_c0_program_add_record(parser->program,
                                             parser->source + name_span.begin.offset,
                                             minic_parser_span_length(name_span),
                                             &record_id)) {
                minic_parser_error(parser, "out of memory while adding record");
                return false;
            }
            parser->program->records[record_id].is_union = is_union;
            parser->program->records[record_id].is_packed = is_packed;
        } else {
            record = minic_c0_program_record(parser->program, record_id);
            if (record == NULL || record->is_complete || record->is_union != is_union ||
                (is_packed && record->is_packed != is_packed)) {
                minic_parser_error(parser, "duplicate record definition");
                return false;
            }
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else if (parser->current.kind == MINIC_TOKEN_LBRACE) {
        if (!minic_c0_program_add_anonymous_record(parser->program, &record_id)) {
            minic_parser_error(parser, "out of memory while adding anonymous record");
            return false;
        }
        parser->program->records[record_id].is_union = is_union;
        parser->program->records[record_id].is_packed = is_packed;
    } else {
        minic_parser_error(parser, "expected record tag or '{' after 'struct'");
        return false;
    }

    if (!minic_parser_expect(parser, MINIC_TOKEN_LBRACE, "expected '{' after record specifier")) {
        return false;
    }
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        if (parser->current.kind == MINIC_TOKEN_EOF) {
            minic_parser_error(parser, "expected '}' before end of file");
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
