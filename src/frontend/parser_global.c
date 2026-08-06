#include "frontend/parser_internal.h"

#include <stdint.h>
#include <string.h>

MinicGlobalObjectId minic_parser_find_global_object(
    const MinicParser *parser,
    MinicSourceSpan name_span)
{
    size_t name_length;
    size_t index;

    name_length = minic_parser_span_length(name_span);
    for (index = 0U; index < parser->program->global_object_count; ++index) {
        const MinicGlobalObject *object;

        object = minic_c0_program_global_object(parser->program, index);
        if (object != NULL && object->name_length == name_length &&
            memcmp(
                object->name,
                parser->source + name_span.begin.offset,
                name_length) == 0) {
            return index;
        }
    }
    return MINIC_GLOBAL_OBJECT_INVALID;
}

bool minic_parser_parse_static_global(MinicParser *parser)
{
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
    if (!minic_parser_expect(
            parser,
            MINIC_TOKEN_KW_STATIC,
            "expected keyword 'static'") ||
        !minic_parser_parse_type_name(parser, &element_type)) {
        return false;
    }
    if (!minic_type_is_integer(element_type) ||
        !minic_type_is_const(element_type)) {
        minic_parser_error(
            parser,
            "static global arrays currently require const int elements");
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected global object name");
        return false;
    }

    name_span = parser->current.span;
    if (minic_parser_find_global_object(parser, name_span) !=
        MINIC_GLOBAL_OBJECT_INVALID) {
        minic_parser_error(parser, "duplicate global object");
        return false;
    }
    if (!minic_parser_advance(parser)) {
        return false;
    }

    while (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        if (bound_count >= sizeof(bounds) / sizeof(bounds[0])) {
            minic_parser_error(parser, "at most eight array dimensions are supported");
            return false;
        }
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_fixed_array_bound(
                parser,
                &bounds[bound_count])) {
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
                parser->program,
                object_type,
                bounds[index - 1U],
                &object_type)) {
            minic_parser_error(parser, "out of memory while building global array type");
            return false;
        }
    }
    if (!minic_c0_program_add_global_object(
            parser->program,
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
            !minic_c0_global_object_add_initializer(
                parser->program,
                object_id,
                value)) {
            if (parser->diagnostic != NULL &&
                parser->diagnostic->message[0] == '\0') {
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
            if (!minic_c0_global_object_add_initializer(
                    parser->program,
                    object_id,
                    0)) {
                minic_parser_error(parser, "out of memory while zero-filling initializer");
                return false;
            }
            object = minic_c0_program_global_object(parser->program, object_id);
        }
    }
    return minic_parser_expect(
        parser,
        MINIC_TOKEN_SEMICOLON,
        "expected ';' after global object");
}
