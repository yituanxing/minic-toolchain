#include "frontend/parser_internal.h"

#include <stdint.h>
#include <string.h>

MinicGlobalObjectId minic_parser_find_global_object(const MinicParser *parser,
                                                    MinicSourceSpan name_span) {
    MinicGlobalObjectId static_local_id;
    size_t name_length;
    size_t index;

    static_local_id = minic_parser_find_static_local(parser, name_span);
    if (static_local_id != MINIC_GLOBAL_OBJECT_INVALID) {
        return static_local_id;
    }

    name_length = minic_parser_span_length(name_span);
    for (index = 0U; index < parser->program->global_object_count; ++index) {
        const MinicGlobalObject *object;

        object = minic_c0_program_global_object(parser->program, index);
        if (object != NULL && object->name_length == name_length &&
            memcmp(object->name, parser->source + name_span.begin.offset, name_length) == 0) {
            return index;
        }
    }
    return MINIC_GLOBAL_OBJECT_INVALID;
}

static bool token_starts_type_name(MinicTokenKind kind) {
    return kind == MINIC_TOKEN_KW_CONST || kind == MINIC_TOKEN_KW_CHAR ||
           kind == MINIC_TOKEN_KW_FLOAT || kind == MINIC_TOKEN_KW_DOUBLE ||
           kind == MINIC_TOKEN_KW_INT || kind == MINIC_TOKEN_KW_LONG ||
           kind == MINIC_TOKEN_KW_SIGNED || kind == MINIC_TOKEN_KW_UNSIGNED ||
           kind == MINIC_TOKEN_KW_VOID || kind == MINIC_TOKEN_KW_STRUCT ||
           kind == MINIC_TOKEN_IDENTIFIER;
}

static bool parse_zero_pointer_constant(MinicParser *parser) {
    if (parser->current.kind == MINIC_TOKEN_INTEGER_CONSTANT) {
        int value;

        if (!minic_parser_parse_integer_value(parser, &value)) {
            return false;
        }
        if (value != 0) {
            minic_parser_error(parser, "static pointer initializer must be null");
            return false;
        }
        return true;
    }

    if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        MinicType cast_type;

        if (!minic_parser_advance(parser)) {
            return false;
        }
        if (token_starts_type_name(parser->current.kind)) {
            if (!minic_parser_parse_type_name(parser, &cast_type) ||
                !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after null cast") ||
                !minic_type_is_pointer(cast_type)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser, "static null cast requires a pointer type");
                }
                return false;
            }
            return parse_zero_pointer_constant(parser);
        }
        if (!parse_zero_pointer_constant(parser) ||
            !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after null constant")) {
            return false;
        }
        return true;
    }

    minic_parser_error(parser, "static pointer initializer must be null");
    return false;
}

static bool parse_zero_initializer(MinicParser *parser, MinicType type) {
    if (minic_type_is_integer(type)) {
        int value;

        if (!minic_parser_parse_integer_value(parser, &value)) {
            return false;
        }
        if (value != 0) {
            minic_parser_error(parser, "static zero initializer requires integer zero");
            return false;
        }
        return true;
    }
    if (minic_type_is_pointer(type)) {
        return parse_zero_pointer_constant(parser);
    }
    if (minic_type_is_record(type)) {
        const MinicRecord *record;
        size_t field_index;

        record = minic_c0_program_record(parser->program, type.record_id);
        if (record == NULL || !record->is_complete) {
            minic_parser_error(parser, "static record initializer requires a complete record type");
            return false;
        }
        if (!minic_parser_expect(
                parser, MINIC_TOKEN_LBRACE, "expected '{' in record initializer")) {
            return false;
        }

        field_index = 0U;
        while (parser->current.kind != MINIC_TOKEN_RBRACE) {
            const MinicRecordField *field;

            if (field_index >= record->field_count) {
                minic_parser_error(parser, "too many static record initializers");
                return false;
            }
            field = minic_c0_record_field(record, field_index);
            if (field == NULL || field->element_count != 1U ||
                (!minic_type_is_integer(field->type) && !minic_type_is_pointer(field->type) &&
                 !minic_type_is_record(field->type))) {
                minic_parser_error(parser, "static zero record initializer requires scalar fields");
                return false;
            }
            if (!parse_zero_initializer(parser, field->type)) {
                return false;
            }
            field_index += 1U;
            if (parser->current.kind == MINIC_TOKEN_COMMA) {
                if (!minic_parser_advance(parser)) {
                    return false;
                }
                if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                    break;
                }
            } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
                minic_parser_error(parser, "expected ',' or '}' in record initializer");
                return false;
            }
        }
        return minic_parser_expect(
            parser, MINIC_TOKEN_RBRACE, "expected '}' after record initializer");
    }

    minic_parser_error(parser, "unsupported static zero initializer type");
    return false;
}

static bool parse_static_zero_record_global(MinicParser *parser,
                                            MinicType record_type,
                                            MinicSourceSpan name_span) {
    MinicGlobalObjectId object_id;
    const MinicRecord *record;

    record = minic_c0_program_record(parser->program, record_type.record_id);
    if (record == NULL || !record->is_complete) {
        minic_parser_error(parser, "static record global requires a complete record type");
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        minic_parser_error(parser, "static record array globals are not supported");
        return false;
    }
    if (!minic_c0_program_add_global_object(parser->program,
                                            parser->source + name_span.begin.offset,
                                            minic_parser_span_length(name_span),
                                            record_type,
                                            true,
                                            minic_type_is_const(record_type),
                                            &object_id)) {
        minic_parser_error(parser, "cannot add static record global");
        return false;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '='") ||
        !parse_zero_initializer(parser, record_type) ||
        !minic_c0_global_object_set_zero_initialized(parser->program, object_id)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot record zero-initialized global object");
        }
        return false;
    }
    return minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after global object");
}

bool minic_parser_parse_static_global(MinicParser *parser) {
    MinicSourceSpan name_span;
    MinicType element_type;
    MinicType object_type;
    MinicGlobalObjectId object_id;
    size_t bounds[8];
    size_t bound_count;
    size_t expected_count;
    size_t index;

    bound_count = 0U;
    expected_count = 1U;
    if (!minic_parser_expect(parser, MINIC_TOKEN_KW_STATIC, "expected keyword 'static'") ||
        !minic_parser_parse_type_name(parser, &element_type)) {
        return false;
    }
    if ((!minic_type_is_integer(element_type) || !minic_type_is_const(element_type)) &&
        !minic_type_is_record(element_type)) {
        minic_parser_error(parser, "static global arrays currently require const integer elements");
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected global object name");
        return false;
    }

    name_span = parser->current.span;
    if (minic_parser_find_global_object(parser, name_span) != MINIC_GLOBAL_OBJECT_INVALID) {
        minic_parser_error(parser, "duplicate global object");
        return false;
    }
    if (!minic_parser_advance(parser)) {
        return false;
    }

    if (minic_type_is_record(element_type)) {
        return parse_static_zero_record_global(parser, element_type, name_span);
    }

    while (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        if (bound_count >= sizeof(bounds) / sizeof(bounds[0])) {
            minic_parser_error(parser, "at most eight array dimensions are supported");
            return false;
        }
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_fixed_array_bound(parser, &bounds[bound_count])) {
            return false;
        }
        if (expected_count > SIZE_MAX / bounds[bound_count]) {
            minic_parser_error(parser, "global array element count overflows");
            return false;
        }
        expected_count *= bounds[bound_count];
        bound_count += 1U;
    }
    if (bound_count == 0U) {
        minic_parser_error(parser, "static global object requires a fixed array declarator");
        return false;
    }

    object_type = element_type;
    for (index = bound_count; index > 0U; --index) {
        if (!minic_c0_program_add_array_type(
                parser->program, object_type, bounds[index - 1U], &object_type)) {
            minic_parser_error(parser, "out of memory while building global array type");
            return false;
        }
    }
    if (!minic_c0_program_add_global_object(parser->program,
                                            parser->source + name_span.begin.offset,
                                            minic_parser_span_length(name_span),
                                            object_type,
                                            true,
                                            true,
                                            &object_id)) {
        minic_parser_error(parser, "cannot add global object");
        return false;
    }

    if (!minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '='") ||
        !minic_parser_expect(parser, MINIC_TOKEN_LBRACE, "expected '{'")) {
        return false;
    }
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        int value;
        const MinicGlobalObject *object;

        object = minic_c0_program_global_object(parser->program, object_id);
        if (object == NULL || object->initializer_count >= expected_count) {
            minic_parser_error(parser, "too many global array initializers");
            return false;
        }
        if (!minic_parser_parse_integer_value(parser, &value) ||
            !minic_c0_global_object_add_initializer(parser->program, object_id, value)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "out of memory while adding initializer");
            }
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
        } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' in initializer");
            return false;
        }
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}'")) {
        return false;
    }

    {
        const MinicGlobalObject *object;

        object = minic_c0_program_global_object(parser->program, object_id);
        while (object != NULL && object->initializer_count < expected_count) {
            if (!minic_c0_global_object_add_initializer(parser->program, object_id, 0)) {
                minic_parser_error(parser, "out of memory while zero-filling initializer");
                return false;
            }
            object = minic_c0_program_global_object(parser->program, object_id);
        }
    }
    return minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after global object");
}
