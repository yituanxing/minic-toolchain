#include "frontend/parser_internal.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct MinicEnumNumericValue {
    bool negative;
    int64_t signed_value;
    uint64_t unsigned_value;
} MinicEnumNumericValue;

static bool append_enum_tag(MinicParser *parser, MinicSourceSpan name_span, MinicEnumId enum_id) {
    MinicParserEnumTag *resized;
    size_t new_capacity;

    if (parser == NULL || enum_id == MINIC_ENUM_INVALID) {
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
    parser->enum_tags[parser->enum_tag_count].enum_id = enum_id;
    parser->enum_tag_count += 1U;
    return true;
}

MinicEnumId minic_parser_find_enum_tag(const MinicParser *parser, MinicSourceSpan name_span) {
    size_t index;

    if (parser == NULL) {
        return MINIC_ENUM_INVALID;
    }
    for (index = parser->enum_tag_count; index > 0U; --index) {
        if (minic_parser_span_equals(parser, name_span, parser->enum_tags[index - 1U].name_span)) {
            return parser->enum_tags[index - 1U].enum_id;
        }
    }
    return MINIC_ENUM_INVALID;
}

static bool
create_named_enum(MinicParser *parser, MinicSourceSpan name_span, MinicEnumId *enum_id) {
    if (!minic_c0_program_add_enum(parser->program,
                                   parser->source + name_span.begin.offset,
                                   minic_parser_span_length(name_span),
                                   enum_id) ||
        !append_enum_tag(parser, name_span, *enum_id)) {
        minic_parser_error(parser, "cannot create enum tag entity");
        return false;
    }
    return true;
}

static bool
get_or_create_enum_tag(MinicParser *parser, MinicSourceSpan name_span, MinicEnumId *enum_id) {
    *enum_id = minic_parser_find_enum_tag(parser, name_span);
    return *enum_id != MINIC_ENUM_INVALID || create_named_enum(parser, name_span, enum_id);
}

MinicEnumeratorId minic_parser_find_enum_constant(const MinicParser *parser,
                                                  MinicSourceSpan name_span) {
    size_t index;

    if (parser == NULL) {
        return MINIC_ENUMERATOR_INVALID;
    }
    for (index = parser->enum_constant_count; index > 0U; --index) {
        const MinicParserEnumConstant *constant = &parser->enum_constants[index - 1U];

        if (minic_parser_span_equals(parser, name_span, constant->name_span)) {
            return constant->enumerator_id;
        }
    }
    return MINIC_ENUMERATOR_INVALID;
}

bool minic_parser_bind_enum_constant(MinicParser *parser,
                                     MinicSourceSpan name_span,
                                     MinicEnumeratorId enumerator_id) {
    MinicParserEnumConstant *resized;
    size_t new_capacity;

    if (parser == NULL || enumerator_id == MINIC_ENUMERATOR_INVALID ||
        minic_parser_find_enum_constant(parser, name_span) != MINIC_ENUMERATOR_INVALID) {
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
    parser->enum_constants[parser->enum_constant_count].enumerator_id = enumerator_id;
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

static bool
signed_type_fits(MinicParser *parser, MinicType type, int64_t minimum, uint64_t maximum) {
    unsigned int bits;
    int64_t type_minimum;
    uint64_t type_maximum;

    if (!minic_target_info_integer_width(parser->target_info, parser->program, type, &bits) ||
        bits == 0U || bits > 64U || minic_type_is_unsigned_integer(type)) {
        return false;
    }
    if (bits == 64U) {
        type_minimum = INT64_MIN;
        type_maximum = (uint64_t)INT64_MAX;
    } else {
        type_minimum = -(INT64_C(1) << (bits - 1U));
        type_maximum = (UINT64_C(1) << (bits - 1U)) - UINT64_C(1);
    }
    return minimum >= type_minimum && maximum <= type_maximum;
}

static bool unsigned_type_fits(MinicParser *parser, MinicType type, uint64_t maximum) {
    unsigned int bits;
    uint64_t type_maximum;

    if (!minic_target_info_integer_width(parser->target_info, parser->program, type, &bits) ||
        bits == 0U || bits > 64U || !minic_type_is_unsigned_integer(type)) {
        return false;
    }
    type_maximum = bits == 64U ? UINT64_MAX : (UINT64_C(1) << bits) - UINT64_C(1);
    return maximum <= type_maximum;
}

static bool
enum_value_type(MinicParser *parser, const MinicEnumNumericValue *value, MinicType *type) {
    if (value->negative) {
        MinicType candidates[] = {minic_type_int(), minic_type_long(), minic_type_long_long()};
        size_t index;

        for (index = 0U; index < sizeof(candidates) / sizeof(candidates[0]); ++index) {
            if (signed_type_fits(parser, candidates[index], value->signed_value, 0U)) {
                *type = candidates[index];
                return true;
            }
        }
        return false;
    }
    if ((uint64_t)INT32_MAX >= value->unsigned_value) {
        *type = minic_type_int();
        return true;
    }
    {
        MinicType candidates[] = {
            minic_type_unsigned_int(), minic_type_unsigned_long(), minic_type_unsigned_long_long()};
        size_t index;

        for (index = 0U; index < sizeof(candidates) / sizeof(candidates[0]); ++index) {
            if (unsigned_type_fits(parser, candidates[index], value->unsigned_value)) {
                *type = candidates[index];
                return true;
            }
        }
    }
    return false;
}

static bool normalize_value_bits(MinicParser *parser,
                                 const MinicEnumNumericValue *value,
                                 MinicType type,
                                 uint64_t *bits) {
    unsigned int width;
    uint64_t raw;

    if (!minic_target_info_integer_width(parser->target_info, parser->program, type, &width) ||
        width == 0U || width > 64U) {
        return false;
    }
    raw = value->negative ? (uint64_t)value->signed_value : value->unsigned_value;
    if (width != 64U) {
        raw &= (UINT64_C(1) << width) - UINT64_C(1);
    }
    *bits = raw;
    return true;
}

static bool const_value_to_numeric(MinicParser *parser,
                                   const MinicConstValue *constant,
                                   MinicEnumNumericValue *value) {
    unsigned int width;
    uint64_t raw;

    if (constant == NULL || value == NULL || !minic_type_is_integer(constant->type) ||
        !minic_target_info_integer_width(
            parser->target_info, parser->program, constant->type, &width) ||
        width == 0U || width > 64U) {
        return false;
    }
    raw = constant->bits;
    if (width != 64U) {
        raw &= (UINT64_C(1) << width) - UINT64_C(1);
    }
    (void)memset(value, 0, sizeof(*value));
    if (minic_type_is_signed_integer(constant->type) &&
        (raw & (UINT64_C(1) << (width - 1U))) != 0U) {
        if (width != 64U) {
            raw |= ~((UINT64_C(1) << width) - UINT64_C(1));
        }
        (void)memcpy(&value->signed_value, &raw, sizeof(raw));
        value->negative = true;
        return true;
    }
    value->unsigned_value = raw;
    return true;
}

static bool parse_enum_integer_value(MinicParser *parser,
                                     MinicEnumNumericValue *value,
                                     MinicType *value_type,
                                     uint64_t *bits) {
    const MinicExpression *expression;
    MinicConstValue constant_value;
    MinicExpressionId expression_id;

    if (parser == NULL || value == NULL || value_type == NULL || bits == NULL ||
        !minic_parser_parse_expression(parser, &expression_id, 0U)) {
        return false;
    }
    expression = minic_c0_program_expression(parser->program, expression_id);
    if (expression == NULL || !minic_type_is_integer(expression->type) ||
        !minic_const_eval_integer(
            parser->program, parser->target_info, expression_id, &constant_value)) {
        minic_parser_error(parser, "enum initializer must be an integer constant expression");
        return false;
    }
    if (!const_value_to_numeric(parser, &constant_value, value) ||
        !enum_value_type(parser, value, value_type) ||
        !normalize_value_bits(parser, value, *value_type, bits)) {
        minic_parser_error(parser, "enum initializer exceeds the supported 64-bit value range");
        return false;
    }
    return true;
}

static bool next_enum_numeric(MinicParser *parser,
                              const MinicEnumNumericValue *current,
                              MinicEnumNumericValue *next,
                              MinicType *next_type,
                              uint64_t *next_bits) {
    *next = *current;
    if (next->negative) {
        if (next->signed_value == INT64_MAX) {
            return false;
        }
        next->signed_value += 1;
        if (next->signed_value >= 0) {
            next->negative = false;
            next->unsigned_value = (uint64_t)next->signed_value;
        }
    } else {
        if (next->unsigned_value == UINT64_MAX) {
            return false;
        }
        next->unsigned_value += 1U;
    }
    return enum_value_type(parser, next, next_type) &&
           normalize_value_bits(parser, next, *next_type, next_bits);
}

static bool choose_enum_compatible_type(MinicParser *parser,
                                        bool saw_negative,
                                        int64_t minimum,
                                        uint64_t maximum,
                                        MinicType *compatible_type) {
    if (saw_negative) {
        MinicType candidates[] = {minic_type_int(), minic_type_long(), minic_type_long_long()};
        size_t index;

        for (index = 0U; index < sizeof(candidates) / sizeof(candidates[0]); ++index) {
            if (signed_type_fits(parser, candidates[index], minimum, maximum)) {
                *compatible_type = candidates[index];
                return true;
            }
        }
        return false;
    }
    {
        MinicType candidates[] = {
            minic_type_unsigned_int(), minic_type_unsigned_long(), minic_type_unsigned_long_long()};
        size_t index;

        for (index = 0U; index < sizeof(candidates) / sizeof(candidates[0]); ++index) {
            if (unsigned_type_fits(parser, candidates[index], maximum)) {
                *compatible_type = candidates[index];
                return true;
            }
        }
    }
    return false;
}

bool minic_parser_parse_enum_specifier(MinicParser *parser, MinicType *enum_type) {
    MinicSourceSpan tag_span;
    MinicEnumId enum_id;
    MinicEnumNumericValue next_value;
    MinicType next_type;
    uint64_t next_bits;
    bool has_next;
    bool has_tag;
    bool saw_negative;
    int64_t minimum;
    uint64_t maximum;

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
        const MinicEnum *entity;

        if (!has_tag || !get_or_create_enum_tag(parser, tag_span, &enum_id)) {
            if (!has_tag) {
                minic_parser_error(parser, "expected enum tag or definition");
            }
            return false;
        }
        entity = minic_c0_program_enum(parser->program, enum_id);
        if (entity == NULL) {
            minic_parser_error(parser, "invalid enum tag entity");
            return false;
        }
        *enum_type = minic_type_enum(enum_id, entity->compatible_type);
        return !minic_type_is_void(*enum_type);
    }

    if (has_tag) {
        const MinicEnum *existing;

        if (!get_or_create_enum_tag(parser, tag_span, &enum_id)) {
            return false;
        }
        existing = minic_c0_program_enum(parser->program, enum_id);
        if (existing == NULL || existing->is_complete) {
            minic_parser_error(parser, "duplicate enum definition");
            return false;
        }
    } else if (!minic_c0_program_add_enum(parser->program, NULL, 0U, &enum_id)) {
        minic_parser_error(parser, "cannot create anonymous enum entity");
        return false;
    }

    if (!minic_parser_advance(parser)) {
        return false;
    }
    (void)memset(&next_value, 0, sizeof(next_value));
    next_type = minic_type_int();
    next_bits = 0U;
    has_next = true;
    saw_negative = false;
    minimum = 0;
    maximum = 0U;

    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        MinicSourceSpan name_span;
        MinicEnumNumericValue value;
        MinicType value_type;
        uint64_t value_bits;
        MinicEnumeratorId enumerator_id;

        if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
            minic_parser_error(parser, "expected enumerator name");
            return false;
        }
        name_span = parser->current.span;
        if (!minic_parser_advance(parser)) {
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_EQUAL) {
            if (!minic_parser_advance(parser) ||
                !parse_enum_integer_value(parser, &value, &value_type, &value_bits)) {
                return false;
            }
        } else {
            if (!has_next) {
                minic_parser_error(parser,
                                   "implicit enumerator value exceeds current 64-bit range");
                return false;
            }
            value = next_value;
            value_type = next_type;
            value_bits = next_bits;
        }
        if (!minic_c0_program_add_enumerator(parser->program,
                                             enum_id,
                                             parser->source + name_span.begin.offset,
                                             minic_parser_span_length(name_span),
                                             value_type,
                                             value_bits,
                                             &enumerator_id) ||
            !minic_parser_bind_enum_constant(parser, name_span, enumerator_id)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot bind enumerator entity");
            }
            return false;
        }
        if (value.negative) {
            if (!saw_negative || value.signed_value < minimum) {
                minimum = value.signed_value;
            }
            saw_negative = true;
        } else if (value.unsigned_value > maximum) {
            maximum = value.unsigned_value;
        }
        has_next = next_enum_numeric(parser, &value, &next_value, &next_type, &next_bits);

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
    {
        MinicType compatible_type;

        if (!choose_enum_compatible_type(
                parser, saw_negative, minimum, maximum, &compatible_type) ||
            !minic_c0_program_finish_enum(parser->program, enum_id, compatible_type)) {
            minic_parser_error(parser,
                               "enum values do not fit a supported compatible integer type");
            return false;
        }
        *enum_type = minic_type_enum(enum_id, compatible_type);
        return !minic_type_is_void(*enum_type);
    }
}

bool minic_parser_parse_enum_definition(MinicParser *parser) {
    MinicType enum_type;

    return minic_parser_parse_enum_specifier(parser, &enum_type) &&
           minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after enum definition");
}
