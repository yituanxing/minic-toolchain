#include "frontend/parser_internal.h"

#include <stdio.h>
#include <string.h>

static bool decode_simple_escape(char character, int *value) {
    if (value == NULL) {
        return false;
    }
    switch (character) {
    case '\'':
        *value = '\'';
        return true;
    case '"':
        *value = '"';
        return true;
    case '?':
        *value = '?';
        return true;
    case '\\':
        *value = '\\';
        return true;
    case 'a':
        *value = '\a';
        return true;
    case 'b':
        *value = '\b';
        return true;
    case 'f':
        *value = '\f';
        return true;
    case 'n':
        *value = '\n';
        return true;
    case 'r':
        *value = '\r';
        return true;
    case 't':
        *value = '\t';
        return true;
    case 'v':
        *value = '\v';
        return true;
    case '0':
        *value = 0;
        return true;
    default:
        return false;
    }
}

static int hex_digit_value(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

static bool decode_string_escape(const char *source, size_t *cursor, size_t end, int *value) {
    unsigned int decoded;
    int digit;

    if (source == NULL || cursor == NULL || value == NULL || *cursor >= end) {
        return false;
    }
    if (source[*cursor] != 'x') {
        if (!decode_simple_escape(source[*cursor], value)) {
            return false;
        }
        *cursor += 1U;
        return true;
    }

    *cursor += 1U;
    if (*cursor >= end || hex_digit_value(source[*cursor]) < 0) {
        return false;
    }
    decoded = 0U;
    while (*cursor < end && (digit = hex_digit_value(source[*cursor])) >= 0) {
        if (decoded > (255U - (unsigned int)digit) / 16U) {
            return false;
        }
        decoded = decoded * 16U + (unsigned int)digit;
        *cursor += 1U;
    }
    *value = (int)decoded;
    return true;
}

static bool decoded_string_length(MinicParser *parser, MinicSourceSpan span, size_t *length) {
    size_t cursor;
    size_t end;
    size_t result;

    if (parser == NULL || length == NULL || span.end.offset <= span.begin.offset + 1U) {
        return false;
    }
    cursor = span.begin.offset + 1U;
    end = span.end.offset - 1U;
    result = 0U;
    while (cursor < end) {
        if (parser->source[cursor] == '\\') {
            int value;

            cursor += 1U;
            if (!decode_string_escape(parser->source, &cursor, end, &value)) {
                minic_parser_error(parser, "unsupported string escape");
                return false;
            }
        } else {
            cursor += 1U;
        }
        if (result == SIZE_MAX) {
            minic_parser_error(parser, "string literal is too long");
            return false;
        }
        result += 1U;
    }
    *length = result;
    return true;
}

static bool
add_string_initializers(MinicParser *parser, MinicSourceSpan span, MinicGlobalObjectId object_id) {
    size_t cursor;
    size_t end;

    cursor = span.begin.offset + 1U;
    end = span.end.offset - 1U;
    while (cursor < end) {
        int value;

        if (parser->source[cursor] == '\\') {
            cursor += 1U;
            if (!decode_string_escape(parser->source, &cursor, end, &value)) {
                minic_parser_error(parser, "unsupported string escape");
                return false;
            }
        } else {
            value = (int)(unsigned char)parser->source[cursor];
            cursor += 1U;
        }
        if (!minic_c0_global_object_add_initializer(parser->program, object_id, value)) {
            minic_parser_error(parser, "out of memory while storing string literal");
            return false;
        }
    }
    if (!minic_c0_global_object_add_initializer(parser->program, object_id, 0)) {
        minic_parser_error(parser, "out of memory while terminating string literal");
        return false;
    }
    return true;
}

bool minic_parser_parse_string_literal(MinicParser *parser, MinicExpressionId *expression_id) {
    char object_name[64];
    int object_name_length;
    size_t decoded_length;
    MinicSourceSpan span;
    MinicType array_type;
    MinicGlobalObjectId object_id;
    MinicExpression expression;

    if (parser == NULL || expression_id == NULL ||
        parser->current.kind != MINIC_TOKEN_STRING_LITERAL) {
        return false;
    }
    span = parser->current.span;
    if (!decoded_string_length(parser, span, &decoded_length) || decoded_length == SIZE_MAX ||
        !minic_c0_program_add_array_type(
            parser->program, minic_type_char(), decoded_length + 1U, &array_type)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot build string literal array type");
        }
        return false;
    }

    object_name_length = snprintf(object_name,
                                  sizeof(object_name),
                                  ".Lminic_string_%zu",
                                  parser->program->global_object_count);
    if (object_name_length <= 0 || (size_t)object_name_length >= sizeof(object_name) ||
        !minic_c0_program_add_global_object(parser->program,
                                            object_name,
                                            (size_t)object_name_length,
                                            array_type,
                                            true,
                                            true,
                                            &object_id) ||
        !add_string_initializers(parser, span, object_id)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "cannot create string literal object");
        }
        return false;
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_GLOBAL_OBJECT;
    expression.span = span;
    expression.type = array_type;
    expression.value_category = MINIC_VALUE_LVALUE;
    expression.value.global_object_id = object_id;
    return minic_parser_advance(parser) &&
           minic_parser_add_expression(parser, &expression, expression_id);
}
