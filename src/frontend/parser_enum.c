#include "frontend/parser_internal.h"

#include <limits.h>
#include <stdlib.h>

bool minic_parser_find_enum_tag(const MinicParser *parser, MinicSourceSpan name_span) {
    size_t index;

    if (parser == NULL) {
        return false;
    }
    for (index = parser->enum_tag_count; index > 0U; --index) {
        if (minic_parser_span_equals(parser, name_span, parser->enum_tags[index - 1U].name_span)) {
            return true;
        }
    }
    return false;
}

bool minic_parser_bind_enum_tag(MinicParser *parser, MinicSourceSpan name_span) {
    MinicParserEnumTag *resized;
    size_t new_capacity;

    if (parser == NULL || minic_parser_find_enum_tag(parser, name_span)) {
        if (parser != NULL) {
            minic_parser_error(parser, "duplicate enum tag");
        }
        return false;
    }
    if (parser->enum_tag_count == parser->enum_tag_capacity) {
        new_capacity = parser->enum_tag_capacity == 0U ? 8U : parser->enum_tag_capacity * 2U;
        if (new_capacity < parser->enum_tag_capacity ||
            new_capacity > SIZE_MAX / sizeof(*parser->enum_tags)) {
            minic_parser_error(parser, "too many enum tags");
            return false;
        }
        resized = (MinicParserEnumTag *)realloc(parser->enum_tags,
                                                new_capacity * sizeof(*parser->enum_tags));
        if (resized == NULL) {
            minic_parser_error(parser, "out of memory while binding enum tag");
            return false;
        }
        parser->enum_tags = resized;
        parser->enum_tag_capacity = new_capacity;
    }
    parser->enum_tags[parser->enum_tag_count].name_span = name_span;
    parser->enum_tag_count += 1U;
    return true;
}

bool minic_parser_find_enum_constant(const MinicParser *parser,
                                     MinicSourceSpan name_span,
                                     int *value) {
    size_t index;

    if (parser == NULL) {
        return false;
    }
    for (index = parser->enum_constant_count; index > 0U; --index) {
        const MinicParserEnumConstant *constant = &parser->enum_constants[index - 1U];

        if (minic_parser_span_equals(parser, name_span, constant->name_span)) {
            if (value != NULL) {
                *value = constant->value;
            }
            return true;
        }
    }
    return false;
}

bool minic_parser_bind_enum_constant(MinicParser *parser, MinicSourceSpan name_span, int value) {
    MinicParserEnumConstant *resized;
    size_t new_capacity;

    if (parser == NULL || minic_parser_find_enum_constant(parser, name_span, NULL)) {
        if (parser != NULL) {
            minic_parser_error(parser, "duplicate enumerator name");
        }
        return false;
    }
    if (parser->enum_constant_count == parser->enum_constant_capacity) {
        new_capacity =
            parser->enum_constant_capacity == 0U ? 16U : parser->enum_constant_capacity * 2U;
        if (new_capacity < parser->enum_constant_capacity ||
            new_capacity > SIZE_MAX / sizeof(*parser->enum_constants)) {
            minic_parser_error(parser, "too many enum constants");
            return false;
        }
        resized = (MinicParserEnumConstant *)realloc(
            parser->enum_constants, new_capacity * sizeof(*parser->enum_constants));
        if (resized == NULL) {
            minic_parser_error(parser, "out of memory while binding enum constant");
            return false;
        }
        parser->enum_constants = resized;
        parser->enum_constant_capacity = new_capacity;
    }
    parser->enum_constants[parser->enum_constant_count].name_span = name_span;
    parser->enum_constants[parser->enum_constant_count].value = value;
    parser->enum_constant_count += 1U;
    return true;
}

void minic_parser_destroy_enum_constants(MinicParser *parser) {
    if (parser == NULL) {
        return;
    }
    free(parser->enum_constants);
    parser->enum_constants = NULL;
    parser->enum_constant_count = 0U;
    parser->enum_constant_capacity = 0U;
    free(parser->enum_tags);
    parser->enum_tags = NULL;
    parser->enum_tag_count = 0U;
    parser->enum_tag_capacity = 0U;
}

static bool parse_enum_integer_value(MinicParser *parser, int *value) {
    int64_t parsed;

    if (parser == NULL || value == NULL ||
        !minic_parser_parse_integer_constant_expression(parser, &parsed)) {
        return false;
    }
    if (parsed < INT_MIN || parsed > INT_MAX) {
        minic_parser_error(parser, "enum constant expression is out of int range");
        return false;
    }
    *value = (int)parsed;
    return true;
}

bool minic_parser_parse_enum_specifier(MinicParser *parser, MinicType *enum_type) {
    MinicSourceSpan tag_span;
    int next_value;
    bool has_tag;

    if (parser == NULL || enum_type == NULL ||
        !minic_parser_expect(parser, MINIC_TOKEN_KW_ENUM, "expected keyword 'enum'")) {
        return false;
    }
    (void)memset(&tag_span, 0, sizeof(tag_span));
    has_tag = false;
    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        tag_span = parser->current.span;
        has_tag = true;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }

    if (parser->current.kind != MINIC_TOKEN_LBRACE) {
        if (!has_tag || !minic_parser_find_enum_tag(parser, tag_span)) {
            minic_parser_error(parser,
                               has_tag ? "unknown enum tag" : "expected enum tag or definition");
            return false;
        }
        *enum_type = minic_type_int();
        return true;
    }

    if (!minic_parser_advance(parser) ||
        (has_tag && !minic_parser_bind_enum_tag(parser, tag_span))) {
        return false;
    }
    next_value = 0;
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        MinicSourceSpan name_span;
        int value;

        if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
            minic_parser_error(parser, "expected enumerator name");
            return false;
        }
        name_span = parser->current.span;
        if (!minic_parser_advance(parser)) {
            return false;
        }
        value = next_value;
        if (parser->current.kind == MINIC_TOKEN_EQUAL) {
            if (!minic_parser_advance(parser) || !parse_enum_integer_value(parser, &value)) {
                return false;
            }
        }
        if (!minic_parser_bind_enum_constant(parser, name_span, value)) {
            return false;
        }
        next_value = value == INT_MAX ? INT_MAX : value + 1;
        if (parser->current.kind == MINIC_TOKEN_COMMA) {
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                break;
            }
        } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
            minic_parser_error(parser, "expected ',' or '}' after enumerator");
            return false;
        }
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}' after enum definition")) {
        return false;
    }
    *enum_type = minic_type_int();
    return true;
}

bool minic_parser_parse_enum_definition(MinicParser *parser) {
    MinicType enum_type;

    return minic_parser_parse_enum_specifier(parser, &enum_type) &&
           minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after enum definition");
}
