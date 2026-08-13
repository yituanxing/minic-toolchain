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

static bool string_literal_kind(MinicTokenKind kind) {
    return kind == MINIC_TOKEN_STRING_LITERAL || kind == MINIC_TOKEN_WIDE_STRING_LITERAL;
}

static bool string_literal_payload_bounds(const MinicParser *parser,
                                          MinicSourceSpan span,
                                          MinicTokenKind kind,
                                          size_t *cursor,
                                          size_t *end) {
    size_t prefix_length;

    if (parser == NULL || cursor == NULL || end == NULL || !string_literal_kind(kind)) {
        return false;
    }
    prefix_length = kind == MINIC_TOKEN_WIDE_STRING_LITERAL ? 2U : 1U;
    if (span.end.offset <= span.begin.offset + prefix_length ||
        parser->source[span.end.offset - 1U] != '"') {
        return false;
    }
    if (kind == MINIC_TOKEN_WIDE_STRING_LITERAL) {
        if (parser->source[span.begin.offset] != 'L' ||
            parser->source[span.begin.offset + 1U] != '"') {
            return false;
        }
    } else if (parser->source[span.begin.offset] != '"') {
        return false;
    }
    *cursor = span.begin.offset + prefix_length;
    *end = span.end.offset - 1U;
    return true;
}

static bool string_literal_element_type(MinicParser *parser, MinicTokenKind kind, MinicType *type) {
    if (parser == NULL || type == NULL) {
        return false;
    }
    if (kind == MINIC_TOKEN_STRING_LITERAL) {
        *type = minic_type_char();
        return true;
    }
    return kind == MINIC_TOKEN_WIDE_STRING_LITERAL &&
           minic_target_info_wide_character_type(parser->target_info, type);
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

static bool decoded_string_length(MinicParser *parser,
                                  MinicSourceSpan span,
                                  MinicTokenKind kind,
                                  size_t *length) {
    size_t cursor;
    size_t end;
    size_t result;

    if (parser == NULL || length == NULL ||
        !string_literal_payload_bounds(parser, span, kind, &cursor, &end)) {
        return false;
    }
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
    MinicTokenKind literal_kind;
    MinicType element_type;
    size_t decoded_length;
    size_t element_size;
    size_t total_length;

    if (parser == NULL || size == NULL || !string_literal_kind(parser->current.kind)) {
        return false;
    }
    literal_kind = parser->current.kind;
    if (!string_literal_element_type(parser, literal_kind, &element_type)) {
        return false;
    }
    total_length = 0U;
    while (string_literal_kind(parser->current.kind)) {
        if (parser->current.kind != literal_kind) {
            minic_parser_error(parser, "mixed string literal encodings are not supported yet");
            return false;
        }
        if (!decoded_string_length(
                parser, parser->current.span, parser->current.kind, &decoded_length) ||
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
    if (total_length == SIZE_MAX ||
        !minic_target_info_sizeof_type(
            parser->target_info, parser->program, element_type, &element_size) ||
        element_size == 0U || (uint64_t)(total_length + 1U) > UINT64_MAX / element_size) {
        minic_parser_error(parser, "string literal sizeof result is too large");
        return false;
    }
    *size = (uint64_t)(total_length + 1U) * (uint64_t)element_size;
    return true;
}

static bool add_string_payload(MinicParser *parser,
                               MinicSourceSpan span,
                               MinicTokenKind kind,
                               MinicGlobalObjectId object_id) {
    size_t cursor;
    size_t end;

    if (!string_literal_payload_bounds(parser, span, kind, &cursor, &end)) {
        return false;
    }
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
        if (!decoded_string_length(
                &probe, probe.current.span, probe.current.kind, &decoded_length) ||
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
        if (!add_string_payload(parser, literal_span, MINIC_TOKEN_STRING_LITERAL, object_id) ||
            !minic_parser_advance(parser)) {
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

bool minic_parser_get_predefined_function_name_object(MinicParser *parser,
                                                      MinicGlobalObjectId *object_id) {
    const MinicFunction *function;
    MinicGlobalObjectId created_id;
    MinicType array_type;
    MinicType const_char_type;
    char object_name[64];
    int object_name_length;
    size_t index;

    if (parser == NULL || object_id == NULL || parser->current_function == MINIC_FUNCTION_INVALID) {
        if (parser != NULL) {
            minic_parser_error(parser, "__func__ is only available inside a function");
        }
        return false;
    }
    if (parser->current_function_name_object != MINIC_GLOBAL_OBJECT_INVALID) {
        *object_id = parser->current_function_name_object;
        return true;
    }
    function = minic_c0_program_function(parser->program, parser->current_function);
    if (function == NULL || function->name == NULL || function->name_length == 0U ||
        function->name_length == SIZE_MAX) {
        minic_parser_error(parser, "cannot determine predefined function name");
        return false;
    }
    if (!minic_type_add_const(minic_type_char(), &const_char_type) ||
        !minic_c0_program_add_array_type(
            parser->program, const_char_type, function->name_length + 1U, &array_type)) {
        minic_parser_error(parser, "cannot build __func__ array type");
        return false;
    }
    object_name_length = snprintf(object_name,
                                  sizeof(object_name),
                                  ".Lminic_func_name_%zu",
                                  (size_t)parser->current_function);
    if (object_name_length <= 0 || (size_t)object_name_length >= sizeof(object_name) ||
        !minic_c0_program_add_global_object(parser->program,
                                            object_name,
                                            (size_t)object_name_length,
                                            array_type,
                                            true,
                                            true,
                                            &created_id)) {
        minic_parser_error(parser, "cannot create __func__ backing object");
        return false;
    }
    for (index = 0U; index < function->name_length; ++index) {
        if (!minic_c0_global_object_add_initializer(
                parser->program, created_id, (int)(unsigned char)function->name[index])) {
            minic_parser_error(parser, "cannot store __func__ name bytes");
            return false;
        }
    }
    if (!minic_c0_global_object_add_initializer(parser->program, created_id, 0)) {
        minic_parser_error(parser, "cannot terminate __func__ name object");
        return false;
    }
    parser->current_function_name_object = created_id;
    *object_id = created_id;
    return true;
}

bool minic_parser_create_string_literal_object(MinicParser *parser,
                                               MinicGlobalObjectId *object_id,
                                               MinicType *array_type,
                                               MinicSourceSpan *span) {
    MinicParser probe;
    MinicTokenKind literal_kind;
    MinicType element_type;
    char object_name[64];
    int object_name_length;
    size_t decoded_length;
    size_t total_length;
    MinicSourceSpan combined_span;

    if (parser == NULL || object_id == NULL || array_type == NULL || span == NULL ||
        !string_literal_kind(parser->current.kind)) {
        return false;
    }

    literal_kind = parser->current.kind;
    if (!string_literal_element_type(parser, literal_kind, &element_type)) {
        minic_parser_error(parser, "unsupported string literal element type");
        return false;
    }
    probe = *parser;
    combined_span = probe.current.span;
    total_length = 0U;
    while (string_literal_kind(probe.current.kind)) {
        if (probe.current.kind != literal_kind) {
            minic_parser_error(parser, "mixed string literal encodings are not supported yet");
            return false;
        }
        if (!decoded_string_length(
                &probe, probe.current.span, probe.current.kind, &decoded_length) ||
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
            parser->program, element_type, total_length + 1U, array_type)) {
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

    while (string_literal_kind(parser->current.kind)) {
        MinicSourceSpan literal_span;

        if (parser->current.kind != literal_kind) {
            minic_parser_error(parser, "mixed string literal encodings are not supported yet");
            return false;
        }
        literal_span = parser->current.span;
        if (!add_string_payload(parser, literal_span, parser->current.kind, *object_id) ||
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
        if (!decoded_string_length(
                &probe, probe.current.span, probe.current.kind, &decoded_length) ||
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
