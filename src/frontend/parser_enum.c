#include "frontend/parser_internal.h"
#include "common/integer_bits.h"
#include "frontend/parser_name_index.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct MinicEnumNumericValue {
    bool negative;
    int64_t signed_value;
    uint64_t unsigned_value;
} MinicEnumNumericValue;

static bool
create_named_enum(MinicParser *parser, MinicSourceSpan name_span, MinicEnumId *enum_id) {
    if (!minic_c0_program_add_enum(parser->program,
                                   parser->source + name_span.begin.offset,
                                   minic_parser_span_length(name_span),
                                   enum_id) ||
        !minic_parser_bind_enum_tag(parser, name_span, *enum_id)) {
        minic_parser_error(parser, "cannot create enum tag entity");
        return false;
    }
    return true;
}

/* One semantic lookup for all C tag kinds: a different-kind tag is visible,
 * not absent. Only a definition or standalone forward declaration may shadow
 * a binding in an enclosing scope. */
static bool get_or_create_enum_tag(MinicParser *parser, MinicSourceSpan name_span,
                                   bool current_scope_only, bool is_definition,
                                   MinicEnumId *enum_id) {
    const MinicParserTagBinding *tag;

    if (!minic_parser_resolve_tag(parser, name_span, MINIC_PARSER_TAG_ENUM,
                                  current_scope_only, &tag)) {
        return false;
    }
    if (tag != NULL) {
        *enum_id = tag->entity.enum_id;
        return true;
    }
    /* GNU C allows `enum E { ... }` to shadow an outer struct/union E.
     * The nonstandard incomplete `enum E;` does not have that privilege:
     * GCC diagnoses the visible different-kind tag as a constraint failure. */
    if (current_scope_only && !is_definition) {
        const MinicParserTagBinding *visible = minic_parser_find_tag(parser, name_span);

        if (visible != NULL && visible->kind != MINIC_PARSER_TAG_ENUM) {
            minic_parser_error(parser, "enum forward declaration sees a different tag kind");
            return false;
        }
    }
    return create_named_enum(parser, name_span, enum_id);
}

static MinicEnumeratorId
find_enum_constant_in_current_scope(const MinicParser *parser, MinicSourceSpan name_span) {
    size_t begin;
    size_t index;

    if (parser == NULL) {
        return MINIC_ENUMERATOR_INVALID;
    }
    begin = parser->scope_count == 0U
                ? 0U
                : parser->scopes[parser->scope_count - 1U].enum_constant_begin;
    for (index = parser->enum_constant_count; index > begin; --index) {
        const MinicParserEnumConstant *constant = &parser->enum_constants[index - 1U];

        if (minic_parser_span_equals(parser, name_span, constant->name_span)) {
            return constant->enumerator_id;
        }
    }
    return MINIC_ENUMERATOR_INVALID;
}


/* Preserve newest-first enumerator semantics if the optional hash index
 * cannot allocate or synchronize; lookup correctness must not depend on it. */
static MinicEnumeratorId minic_parser_enum_global_linear_fallback(
    const MinicParser *parser, MinicSourceSpan span, size_t global_count) {
    size_t index;
    for (index = global_count; index > 0U; --index) {
        const MinicParserEnumConstant *constant =
            &parser->enum_constants[index - 1U];
        if (minic_parser_span_equals(parser, span, constant->name_span)) {
            return constant->enumerator_id;
        }
    }
    return MINIC_ENUMERATOR_INVALID;
}

MinicEnumeratorId minic_parser_find_enum_constant(
    const MinicParser *parser_const,
    MinicSourceSpan name_span) {
    MinicParser *parser = (MinicParser *)parser_const;
    size_t global_count;
    size_t index;
    bool consistent;
    size_t constant_index;

    if (parser == NULL) {
        return MINIC_ENUMERATOR_INVALID;
    }
    parser->perf_find_enum_calls += 1U;

    /* Enumerators added after the outermost active scope began are local to
       that function/block lifetime and may shadow file-scope names. Keep the
       existing newest-first semantics for this usually-small suffix. */
    global_count = parser->scope_count == 0U
                       ? parser->enum_constant_count
                       : parser->scopes[0].enum_constant_begin;
    for (index = parser->enum_constant_count; index > global_count; --index) {
        const MinicParserEnumConstant *constant =
            &parser->enum_constants[index - 1U];
        parser->perf_find_enum_steps += 1U;
        if (minic_parser_span_equals(parser, name_span, constant->name_span)) {
            return constant->enumerator_id;
        }
    }

    if (global_count == 0U) {
        return MINIC_ENUMERATOR_INVALID;
    }
#if defined(MINIC_TEST_FORCE_PARSER_HASH_FALLBACK)
    return minic_parser_enum_global_linear_fallback(
        parser, name_span, global_count);
#endif
    if (parser->cache_owner != parser ||
        !minic_parser_name_index_sync(parser, MINIC_PARSER_NAME_ENUM,
            global_count, &parser->perf_enum_name_slots,
            &parser->perf_enum_name_slot_capacity,
            &parser->perf_enum_name_indexed_count, NULL) ||
        parser->perf_enum_name_slot_capacity == 0U) {
        return minic_parser_enum_global_linear_fallback(parser, name_span, global_count);
    }
    constant_index = minic_parser_name_index_lookup(
        parser, MINIC_PARSER_NAME_ENUM, parser->perf_enum_name_slots,
        parser->perf_enum_name_slot_capacity,
        parser->source + name_span.begin.offset, minic_parser_span_length(name_span),
        &parser->perf_find_enum_steps, &consistent);
    if (consistent)
        return constant_index == SIZE_MAX ? MINIC_ENUMERATOR_INVALID
            : parser->enum_constants[constant_index].enumerator_id;
    return minic_parser_enum_global_linear_fallback(
        parser, name_span, global_count);
}


bool minic_parser_bind_enum_constant(MinicParser *parser,
                                     MinicSourceSpan name_span,
                                     MinicEnumeratorId enumerator_id) {
    MinicParserEnumConstant *resized;
    size_t new_capacity;

    if (parser == NULL || enumerator_id == MINIC_ENUMERATOR_INVALID ||
        find_enum_constant_in_current_scope(parser, name_span) != MINIC_ENUMERATOR_INVALID ||
        (parser->scope_count != 0U &&
         minic_parser_name_bound_in_current_scope(parser, name_span))) {
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
}

static bool
signed_type_fits(MinicParser *parser, MinicType type, int64_t minimum, uint64_t maximum) {
    unsigned int bits;
    int64_t type_minimum;
    uint64_t type_maximum;

    if (!minic_target_info_integer_width(parser->invocation, parser->program, type, &bits) ||
        bits == 0U || bits > 64U || minic_type_is_unsigned_integer(type)) {
        return false;
    }
    if (bits == 64U) {
        type_minimum = INT64_MIN;
        type_maximum = (uint64_t)INT64_MAX;
    } else {
        type_minimum = -(INT64_C(1) << (bits - 1U));
        type_maximum = minic_integer_mask64(bits - 1U);
    }
    return minimum >= type_minimum && maximum <= type_maximum;
}

static bool unsigned_type_fits(MinicParser *parser, MinicType type, uint64_t maximum) {
    unsigned int bits;
    uint64_t type_maximum;

    if (!minic_target_info_integer_width(parser->invocation, parser->program, type, &bits) ||
        bits == 0U || bits > 64U || !minic_type_is_unsigned_integer(type)) {
        return false;
    }
    type_maximum = minic_integer_mask64(bits);
    return maximum <= type_maximum;
}

static bool choose_enum_compatible_type(MinicParser *parser,
                                        bool saw_negative, bool packed,
                                        int64_t minimum, uint64_t maximum,
                                        MinicType *compatible_type) {
    const MinicType candidates[] = {
        saw_negative ? minic_type_signed_char() : minic_type_unsigned_char(),
        saw_negative ? minic_type_short() : minic_type_unsigned_short(),
        saw_negative ? minic_type_int() : minic_type_unsigned_int(),
        saw_negative ? minic_type_long() : minic_type_unsigned_long(),
        saw_negative ? minic_type_long_long() : minic_type_unsigned_long_long()
    };
    size_t index;
    for (index = packed ? 0U : 2U; index < sizeof(candidates) / sizeof(candidates[0]); ++index) {
        if (saw_negative ? signed_type_fits(parser, candidates[index], minimum, maximum)
                         : unsigned_type_fits(parser, candidates[index], maximum)) {
            *compatible_type = candidates[index];
            return true;
        }
    }
    return false;
}

static bool
enum_value_type(MinicParser *parser, const MinicEnumNumericValue *value, MinicType *type) {
    if (!value->negative && value->unsigned_value <= (uint64_t)INT32_MAX) {
        *type = minic_type_int();
        return true;
    }
    return choose_enum_compatible_type(parser, value->negative, false,
        value->negative ? value->signed_value : 0,
        value->negative ? 0U : value->unsigned_value, type);
}

static bool normalize_value_bits(MinicParser *parser,
                                 const MinicEnumNumericValue *value,
                                 MinicType type,
                                 uint64_t *bits) {
    unsigned int width;
    uint64_t raw;

    if (!minic_target_info_integer_width(parser->invocation, parser->program, type, &width) ||
        width == 0U || width > 64U) {
        return false;
    }
    raw = value->negative ? (uint64_t)value->signed_value : value->unsigned_value;
    raw &= minic_integer_mask64(width);
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
            parser->invocation, parser->program, constant->type, &width) ||
        width == 0U || width > 64U) {
        return false;
    }
    raw = constant->bits;
    raw &= minic_integer_mask64(width);
    (void)memset(value, 0, sizeof(*value));
    if (minic_type_is_signed_integer(constant->type) &&
        (raw & (UINT64_C(1) << (width - 1U))) != 0U) {
        raw = minic_integer_sign_extend64(raw, width);
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
            parser->program, parser->invocation, expression_id, &constant_value)) {
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

typedef struct MinicEnumAttributeContext {
    bool has_byte_mode;
    bool has_packed;
} MinicEnumAttributeContext;

static bool enum_mode_argument_is_byte(const MinicParser *parser,
                                       const MinicParsedAttribute *attribute) {
    size_t begin;
    size_t end;
    const char *value;
    size_t length;

    if (parser == NULL || attribute == NULL || !attribute->has_arguments ||
        attribute->arguments_span.end.offset <= attribute->arguments_span.begin.offset + 1U) {
        return false;
    }
    begin = attribute->arguments_span.begin.offset + 1U;
    end = attribute->arguments_span.end.offset - 1U;
    while (begin < end && (parser->source[begin] == ' ' || parser->source[begin] == '\t' ||
                           parser->source[begin] == '\n' || parser->source[begin] == '\r' ||
                           parser->source[begin] == '\f' || parser->source[begin] == '\v')) {
        begin += 1U;
    }
    while (end > begin && (parser->source[end - 1U] == ' ' || parser->source[end - 1U] == '\t' ||
                           parser->source[end - 1U] == '\n' || parser->source[end - 1U] == '\r' ||
                           parser->source[end - 1U] == '\f' || parser->source[end - 1U] == '\v')) {
        end -= 1U;
    }
    value = parser->source + begin;
    length = end - begin;
    return (length == 4U && memcmp(value, "byte", 4U) == 0) ||
           (length == 8U && memcmp(value, "__byte__", 8U) == 0);
}

static bool consume_enum_attribute(MinicParser *parser,
                                   const MinicParsedAttribute *attribute,
                                   void *opaque_context) {
    MinicEnumAttributeContext *context;

    if (parser == NULL || attribute == NULL || opaque_context == NULL) {
        return false;
    }
    context = (MinicEnumAttributeContext *)opaque_context;
    if (attribute->descriptor == NULL ||
        !minic_attribute_allowed_on(attribute->descriptor, MINIC_ATTRIBUTE_TARGET_TYPE)) {
        minic_parser_error(parser, "unsupported GNU enum type attribute");
        return false;
    }
    if (attribute->descriptor->kind == MINIC_ATTRIBUTE_MODE) {
        if (!enum_mode_argument_is_byte(parser, attribute)) {
            minic_parser_error(parser, "unsupported GNU enum mode; only mode(byte) is implemented");
            return false;
        }
        context->has_byte_mode = true;
        return true;
    }
    if (attribute->descriptor->kind == MINIC_ATTRIBUTE_PACKED) {
        context->has_packed = true;
        return true;
    }
    minic_parser_error(parser, "unsupported GNU enum type attribute");
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
    MinicEnumAttributeContext attribute_context;
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

        /* A tag-only declaration (`enum E;`) introduces/redeclares E in
         * THIS scope; `enum E value` merely refers to the closest visible
         * tag. This mirrors the record-tag scope model and GNU C. */
        if (!has_tag ||
            !get_or_create_enum_tag(parser, tag_span,
                                    parser->current.kind == MINIC_TOKEN_SEMICOLON,
                                    false, &enum_id)) {
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
        *enum_type = minic_type_enum(enum_id);
        return !minic_type_is_void(*enum_type);
    }

    if (has_tag) {
        const MinicEnum *existing;

        if (!get_or_create_enum_tag(parser, tag_span, true, true, &enum_id)) {
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
    (void)memset(&attribute_context, 0, sizeof(attribute_context));
    if (!minic_parser_parse_gnu_attribute_lists(
            parser, consume_enum_attribute, &attribute_context)) {
        return false;
    }
    {
        MinicType compatible_type;
        bool type_fits;

        if (attribute_context.has_byte_mode) {
            compatible_type = saw_negative ? minic_type_signed_char() : minic_type_unsigned_char();
            type_fits = saw_negative ? signed_type_fits(parser, compatible_type, minimum, maximum)
                                     : unsigned_type_fits(parser, compatible_type, maximum);
            if (!type_fits) {
                minic_parser_error(parser, "enum mode(byte) cannot represent enumerator values");
                return false;
            }
        } else if (!choose_enum_compatible_type(
                       parser, saw_negative, attribute_context.has_packed,
                       minimum, maximum, &compatible_type)) {
            minic_parser_error(parser, attribute_context.has_packed
                ? "packed enum values do not fit a supported integer type"
                : "enum values do not fit a supported compatible integer type");
            return false;
        }
        if (!minic_c0_program_finish_enum(parser->program, enum_id, compatible_type)) {
            minic_parser_error(parser, "cannot finish enum compatible integer type");
            return false;
        }
        *enum_type = minic_type_enum(enum_id);
        return !minic_type_is_void(*enum_type);
    }
}

bool minic_parser_parse_enum_definition(MinicParser *parser) {
    MinicType enum_type;

    return minic_parser_parse_enum_specifier(parser, &enum_type) &&
           minic_parser_expect(parser, MINIC_TOKEN_SEMICOLON, "expected ';' after enum definition");
}
