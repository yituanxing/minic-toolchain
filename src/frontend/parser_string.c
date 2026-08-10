#include "frontend/parser_internal.h"

#include <stdio.h>
#include <stdlib.h>
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

bool minic_parser_parse_string_literal_size(MinicParser *parser, uint64_t *size) {
    size_t decoded_length;
    size_t total_length;

    if (parser == NULL || size == NULL || parser->current.kind != MINIC_TOKEN_STRING_LITERAL) {
        return false;
    }
    total_length = 0U;
    while (parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
        if (!decoded_string_length(parser, parser->current.span, &decoded_length) ||
            total_length > SIZE_MAX - decoded_length) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "concatenated string literal is too long");
            }
            return false;
        }
        total_length += decoded_length;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    if (total_length == SIZE_MAX) {
        minic_parser_error(parser, "string literal sizeof result is too large");
        return false;
    }
    *size = (uint64_t)(total_length + 1U);
    return true;
}

static bool
add_string_payload(MinicParser *parser, MinicSourceSpan span, MinicGlobalObjectId object_id) {
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
    return true;
}

bool minic_parser_add_string_literal_initializer(MinicParser *parser,
                                                 MinicGlobalObjectId object_id,
                                                 size_t *element_count) {
    MinicParser probe;
    size_t decoded_length;
    size_t total_length;

    if (parser == NULL || element_count == NULL ||
        parser->current.kind != MINIC_TOKEN_STRING_LITERAL) {
        return false;
    }
    probe = *parser;
    total_length = 0U;
    while (probe.current.kind == MINIC_TOKEN_STRING_LITERAL) {
        if (!decoded_string_length(&probe, probe.current.span, &decoded_length) ||
            total_length > SIZE_MAX - decoded_length || !minic_parser_advance(&probe)) {
            return false;
        }
        total_length += decoded_length;
    }
    if (total_length == SIZE_MAX) {
        minic_parser_error(parser, "concatenated string literal is too long");
        return false;
    }
    while (parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
        MinicSourceSpan literal_span;

        literal_span = parser->current.span;
        if (!add_string_payload(parser, literal_span, object_id) || !minic_parser_advance(parser)) {
            return false;
        }
    }
    if (!minic_c0_global_object_add_initializer(parser->program, object_id, 0)) {
        minic_parser_error(parser, "out of memory while terminating string initializer");
        return false;
    }
    *element_count = total_length + 1U;
    return true;
}

bool minic_parser_create_string_literal_object(MinicParser *parser,
                                               MinicGlobalObjectId *object_id,
                                               MinicType *array_type,
                                               MinicSourceSpan *span) {
    MinicParser probe;
    char object_name[64];
    int object_name_length;
    size_t decoded_length;
    size_t total_length;
    MinicSourceSpan combined_span;

    if (parser == NULL || object_id == NULL || array_type == NULL || span == NULL ||
        parser->current.kind != MINIC_TOKEN_STRING_LITERAL) {
        return false;
    }

    probe = *parser;
    combined_span = probe.current.span;
    total_length = 0U;
    while (probe.current.kind == MINIC_TOKEN_STRING_LITERAL) {
        if (!decoded_string_length(&probe, probe.current.span, &decoded_length) ||
            total_length > SIZE_MAX - decoded_length) {
            if (probe.diagnostic != NULL && probe.diagnostic->message[0] == '\0') {
                minic_parser_error(&probe, "concatenated string literal is too long");
            }
            return false;
        }
        total_length += decoded_length;
        combined_span.end = probe.current.span.end;
        if (!minic_parser_advance(&probe)) {
            return false;
        }
    }
    if (total_length == SIZE_MAX ||
        !minic_c0_program_add_array_type(
            parser->program, minic_type_char(), total_length + 1U, array_type)) {
        minic_parser_error(parser, "cannot build string literal array type");
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
                                            *array_type,
                                            true,
                                            true,
                                            object_id)) {
        minic_parser_error(parser, "cannot create string literal object");
        return false;
    }

    while (parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
        MinicSourceSpan literal_span;

        literal_span = parser->current.span;
        if (!add_string_payload(parser, literal_span, *object_id) ||
            !minic_parser_advance(parser)) {
            return false;
        }
    }
    if (!minic_c0_global_object_add_initializer(parser->program, *object_id, 0)) {
        minic_parser_error(parser, "out of memory while terminating string literal");
        return false;
    }
    *span = combined_span;
    return true;
}

bool minic_parser_parse_string_text(MinicParser *parser,
                                    char **text,
                                    size_t *length,
                                    MinicSourceSpan *span) {
    MinicParser probe;
    MinicSourceSpan combined_span;
    char *buffer;
    size_t total_length;
    size_t decoded_length;
    size_t output;

    if (parser == NULL || text == NULL || length == NULL || span == NULL ||
        parser->current.kind != MINIC_TOKEN_STRING_LITERAL) {
        return false;
    }
    probe = *parser;
    combined_span = probe.current.span;
    total_length = 0U;
    while (probe.current.kind == MINIC_TOKEN_STRING_LITERAL) {
        if (!decoded_string_length(&probe, probe.current.span, &decoded_length) ||
            total_length > SIZE_MAX - decoded_length) {
            minic_parser_error(parser, "inline assembly string is too long");
            return false;
        }
        total_length += decoded_length;
        combined_span.end = probe.current.span.end;
        if (!minic_parser_advance(&probe)) {
            return false;
        }
    }
    if (total_length == SIZE_MAX) {
        minic_parser_error(parser, "inline assembly string is too long");
        return false;
    }
    buffer = (char *)malloc(total_length + 1U);
    if (buffer == NULL) {
        minic_parser_error(parser, "out of memory while decoding inline assembly string");
        return false;
    }

    output = 0U;
    while (parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
        size_t cursor;
        size_t end;

        cursor = parser->current.span.begin.offset + 1U;
        end = parser->current.span.end.offset - 1U;
        while (cursor < end) {
            int value;

            if (parser->source[cursor] == '\\') {
                cursor += 1U;
                if (!decode_string_escape(parser->source, &cursor, end, &value)) {
                    free(buffer);
                    minic_parser_error(parser, "unsupported inline assembly string escape");
                    return false;
                }
            } else {
                value = (int)(unsigned char)parser->source[cursor];
                cursor += 1U;
            }
            if (value == 0) {
                free(buffer);
                minic_parser_error(parser, "inline assembly template cannot contain NUL");
                return false;
            }
            buffer[output] = (char)value;
            output += 1U;
        }
        if (!minic_parser_advance(parser)) {
            free(buffer);
            return false;
        }
    }
    buffer[output] = '\0';
    *text = buffer;
    *length = output;
    *span = combined_span;
    return true;
}

bool minic_parser_parse_string_literal(MinicParser *parser, MinicExpressionId *expression_id) {
    MinicSourceSpan span;
    MinicType array_type;
    MinicGlobalObjectId object_id;
    MinicExpression expression;

    if (parser == NULL || expression_id == NULL ||
        !minic_parser_create_string_literal_object(parser, &object_id, &array_type, &span)) {
        return false;
    }

    (void)memset(&expression, 0, sizeof(expression));
    expression.kind = MINIC_EXPRESSION_GLOBAL_OBJECT;
    expression.span = span;
    expression.type = array_type;
    expression.value_category = MINIC_VALUE_LVALUE;
    expression.value.global_object_id = object_id;
    return minic_parser_add_expression(parser, &expression, expression_id);
}
