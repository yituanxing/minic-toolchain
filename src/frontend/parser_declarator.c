#include "frontend/parser_internal.h"
#include "frontend/semantic_snapshot.h"

#include <limits.h>
#include <string.h>

static bool declarator_identifier_is(const MinicParser *parser, const char *text) {
    size_t length;

    if (parser == NULL || text == NULL || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        return false;
    }
    length = minic_parser_span_length(parser->current.span);
    return strlen(text) == length &&
           memcmp(parser->source + parser->current.span.begin.offset, text, length) == 0;
}

static bool rollback_declarator_type_transaction(MinicParser *parser,
                                                 const MinicSemanticSnapshot *snapshot) {
    if (parser != NULL && parser->program != NULL && snapshot != NULL &&
        minic_semantic_snapshot_rollback_declarator_types(snapshot, parser->program)) {
        return true;
    }
    if (parser != NULL) {
        minic_parser_error(parser, "internal error: declarator type transaction escaped");
    }
    return false;
}

bool minic_parser_parse_pointer_qualifier_sequence(MinicParser *parser,
                                                   size_t pointer_depth,
                                                   unsigned int *const_qualifiers,
                                                   unsigned int *volatile_qualifiers) {
    unsigned int bit;

    if (parser == NULL || const_qualifiers == NULL || volatile_qualifiers == NULL ||
        pointer_depth == 0U || pointer_depth > sizeof(unsigned int) * CHAR_BIT) {
        return false;
    }
    bit = 1U << (pointer_depth - 1U);
    while (parser->current.kind == MINIC_TOKEN_KW_CONST ||
           parser->current.kind == MINIC_TOKEN_KW_VOLATILE ||
           declarator_identifier_is(parser, "restrict") ||
           declarator_identifier_is(parser, "__restrict") ||
           declarator_identifier_is(parser, "__restrict__")) {
        if (parser->current.kind == MINIC_TOKEN_KW_CONST) {
            *const_qualifiers |= bit;
        } else if (parser->current.kind == MINIC_TOKEN_KW_VOLATILE) {
            *volatile_qualifiers |= bit;
        }
        /* restrict remains a parse-only aliasing promise until alias-aware optimization exists. */
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    return true;
}

bool minic_parser_parse_direct_declarator_name(MinicParser *parser, MinicSourceSpan *name_span) {
    size_t parenthesis_depth;

    if (parser == NULL || name_span == NULL) {
        return false;
    }
    parenthesis_depth = 0U;
    while (parser->current.kind == MINIC_TOKEN_LPAREN) {
        parenthesis_depth += 1U;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected declarator name");
        return false;
    }
    *name_span = parser->current.span;
    if (!minic_parser_advance(parser)) {
        return false;
    }
    while (parenthesis_depth > 0U) {
        if (!minic_parser_expect(
                parser, MINIC_TOKEN_RPAREN, "expected ')' after declarator name")) {
            return false;
        }
        parenthesis_depth -= 1U;
    }
    return true;
}

bool minic_parser_parse_function_parameter_suffix(MinicParser *parser,
                                                  MinicParsedFunctionDeclarator *declarator) {
    if (parser == NULL || declarator == NULL) {
        return false;
    }
    declarator->parameter_count = 0U;
    declarator->is_variadic = false;
    return minic_parser_expect(
               parser, MINIC_TOKEN_LPAREN, "expected '(' before function parameter list") &&
           minic_parser_parse_parameter_list(parser,
                                             NULL,
                                             declarator->parameter_types,
                                             &declarator->parameter_count,
                                             false,
                                             &declarator->is_variadic) &&
           minic_parser_expect(
               parser, MINIC_TOKEN_RPAREN, "expected ')' after function parameter list");
}

bool minic_parser_parse_parenthesized_pointer_to_array_declarator(MinicParser *parser,
                                                                  MinicType element_type,
                                                                  MinicSourceSpan *name_span,
                                                                  MinicType *declarator_type) {
    MinicType type;
    size_t pointer_depth;
    size_t level;
    unsigned int pointer_const_qualifiers;
    unsigned int pointer_volatile_qualifiers;
    bool is_array;

    if (parser == NULL || name_span == NULL || declarator_type == NULL ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_LPAREN, "expected '(' before parenthesized pointer declarator")) {
        return false;
    }
    pointer_depth = 0U;
    pointer_const_qualifiers = 0U;
    pointer_volatile_qualifiers = 0U;
    while (parser->current.kind == MINIC_TOKEN_STAR) {
        pointer_depth += 1U;
        if (!minic_parser_advance(parser) ||
            !minic_parser_parse_pointer_qualifier_sequence(
                parser, pointer_depth, &pointer_const_qualifiers, &pointer_volatile_qualifiers)) {
            return false;
        }
    }
    if (pointer_depth == 0U || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "pointer-to-array declarator requires a named pointer");
        return false;
    }
    *name_span = parser->current.span;
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(
            parser, MINIC_TOKEN_RPAREN, "expected ')' after parenthesized pointer declarator") ||
        !minic_parser_parse_array_declarator_suffix(
            parser, element_type, false, &type, &is_array) ||
        !is_array) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser, "parenthesized object pointer requires an array suffix");
        }
        return false;
    }
    for (level = 0U; level < pointer_depth; ++level) {
        unsigned int bit;

        if (!minic_type_pointer_to(type, &type)) {
            minic_parser_error(parser, "pointer-to-array declarator depth is unsupported");
            return false;
        }
        bit = 1U << level;
        if ((pointer_const_qualifiers & bit) != 0U && !minic_type_add_const(type, &type)) {
            return false;
        }
        if ((pointer_volatile_qualifiers & bit) != 0U && !minic_type_add_volatile(type, &type)) {
            return false;
        }
    }
    *declarator_type = type;
    return true;
}

static bool parse_array_bound_allow_zero(MinicParser *parser, size_t *element_count);

typedef struct MinicParsedArraySuffix {
    size_t bounds[8];
    size_t dimension_count;
    unsigned int zero_length_mask;
    bool outermost_incomplete;
} MinicParsedArraySuffix;

static bool parse_array_suffix_syntax(MinicParser *parser,
                                      bool allow_incomplete_outermost,
                                      MinicParsedArraySuffix *suffix) {
    size_t dimension;

    if (parser == NULL || suffix == NULL) {
        return false;
    }
    (void)memset(suffix, 0, sizeof(*suffix));
    while (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        if (suffix->dimension_count >= sizeof(suffix->bounds) / sizeof(suffix->bounds[0])) {
            minic_parser_error(parser, "array declarator supports at most eight dimensions");
            return false;
        }
        dimension = suffix->dimension_count;
        if (!minic_parser_advance(parser)) {
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_RBRACKET) {
            if (!allow_incomplete_outermost || dimension != 0U || suffix->outermost_incomplete) {
                minic_parser_error(parser, "only the outermost array dimension may be incomplete");
                return false;
            }
            suffix->outermost_incomplete = true;
            suffix->bounds[dimension] = 0U;
            if (!minic_parser_advance(parser)) {
                return false;
            }
        } else {
            if (!parse_array_bound_allow_zero(parser, &suffix->bounds[dimension])) {
                return false;
            }
            if (suffix->bounds[dimension] == 0U) {
                suffix->zero_length_mask |= 1U << dimension;
            }
        }
        suffix->dimension_count += 1U;
    }
    return true;
}

static bool materialize_array_suffix_type(MinicParser *parser,
                                          MinicType element_type,
                                          const MinicParsedArraySuffix *suffix,
                                          const char *incomplete_error,
                                          const char *zero_length_error,
                                          const char *array_error,
                                          MinicType *type) {
    MinicSemanticSnapshot snapshot;
    size_t dimension;
    MinicType result;

    if (parser == NULL || suffix == NULL || type == NULL) {
        return false;
    }
    snapshot = minic_semantic_snapshot_capture(parser->program);
    result = element_type;
    dimension = suffix->dimension_count;
    while (dimension > 0U) {
        unsigned int bit;

        dimension -= 1U;
        bit = 1U << dimension;
        if (dimension == 0U && suffix->outermost_incomplete) {
            if (!minic_c0_program_add_incomplete_array_type(parser->program, result, &result)) {
                if (!rollback_declarator_type_transaction(parser, &snapshot)) {
                    return false;
                }
                if (incomplete_error != NULL) {
                    minic_parser_error(parser, "%s", incomplete_error);
                }
                return false;
            }
        } else if ((suffix->zero_length_mask & bit) != 0U) {
            if (!minic_c0_program_add_zero_length_array_type(parser->program, result, &result)) {
                if (!rollback_declarator_type_transaction(parser, &snapshot)) {
                    return false;
                }
                if (zero_length_error != NULL) {
                    minic_parser_error(parser, "%s", zero_length_error);
                }
                return false;
            }
        } else if (!minic_c0_program_add_array_type(
                       parser->program, result, suffix->bounds[dimension], &result)) {
            if (!rollback_declarator_type_transaction(parser, &snapshot)) {
                return false;
            }
            if (array_error != NULL) {
                minic_parser_error(parser, "%s", array_error);
            }
            return false;
        }
    }
    *type = result;
    return true;
}

static bool parse_parenthesized_function_array_suffix(MinicParser *parser,
                                                      MinicParsedFunctionDeclarator *declarator) {
    MinicParsedArraySuffix suffix;

    if (parser == NULL || declarator == NULL ||
        !parse_array_suffix_syntax(parser, true, &suffix)) {
        return false;
    }
    (void)memcpy(declarator->array_bounds, suffix.bounds, sizeof(declarator->array_bounds));
    declarator->array_dimension_count = suffix.dimension_count;
    declarator->array_zero_length_mask = suffix.zero_length_mask;
    declarator->array_outermost_incomplete = suffix.outermost_incomplete;
    return true;
}

bool minic_parser_parse_parenthesized_function_declarator(
    MinicParser *parser,
    bool require_name,
    bool require_pointer,
    MinicParsedFunctionDeclarator *declarator) {
    if (parser == NULL || declarator == NULL) {
        return false;
    }

    (void)memset(declarator, 0, sizeof(*declarator));
    if (!minic_parser_expect(
            parser, MINIC_TOKEN_LPAREN, "expected '(' before function declarator")) {
        return false;
    }
    while (parser->current.kind == MINIC_TOKEN_STAR) {
        declarator->pointer_depth += 1U;
        if (!minic_parser_advance(parser) || !minic_parser_parse_pointer_qualifier_sequence(
                                                 parser,
                                                 declarator->pointer_depth,
                                                 &declarator->pointer_const_qualifiers,
                                                 &declarator->pointer_volatile_qualifiers)) {
            return false;
        }
    }
    if (require_pointer && declarator->pointer_depth == 0U) {
        minic_parser_error(parser, "function declarator requires pointer indirection");
        return false;
    }
    if (!minic_parser_collect_gnu_attribute_lists(parser, &declarator->attributes)) {
        return false;
    }

    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        declarator->name_span = parser->current.span;
        declarator->has_name = true;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else if (require_name) {
        minic_parser_error(parser, "expected function declarator name");
        return false;
    }

    return parse_parenthesized_function_array_suffix(parser, declarator) &&
           minic_parser_expect(
               parser, MINIC_TOKEN_RPAREN, "expected ')' after function declarator") &&
           minic_parser_parse_function_parameter_suffix(parser, declarator);
}

static bool parse_array_bound_allow_zero(MinicParser *parser, size_t *element_count) {
    int64_t value;

    if (parser == NULL || element_count == NULL ||
        !minic_parser_parse_typed_integer_constant_expression(parser, &value)) {
        return false;
    }
    if (value < 0) {
        minic_parser_error(parser, "array bound must not be negative");
        return false;
    }
    if ((uint64_t)value > (uint64_t)SIZE_MAX) {
        minic_parser_error(parser, "array bound exceeds target object range");
        return false;
    }
    if (!minic_parser_expect(parser, MINIC_TOKEN_RBRACKET, "expected ']'")) {
        return false;
    }
    *element_count = (size_t)value;
    return true;
}

bool minic_parser_parse_array_declarator_suffix(MinicParser *parser,
                                                MinicType element_type,
                                                bool allow_incomplete_outermost,
                                                MinicType *declarator_type,
                                                bool *is_array) {
    MinicParsedArraySuffix suffix;

    if (parser == NULL || declarator_type == NULL || is_array == NULL) {
        return false;
    }
    *declarator_type = element_type;
    *is_array = false;
    if (!parse_array_suffix_syntax(parser, allow_incomplete_outermost, &suffix)) {
        return false;
    }
    if (suffix.dimension_count == 0U) {
        return true;
    }
    if (!materialize_array_suffix_type(parser,
                                       element_type,
                                       &suffix,
                                       "cannot build incomplete array declarator type",
                                       "cannot build GNU zero-length array declarator type",
                                       "cannot build array declarator type",
                                       declarator_type)) {
        return false;
    }
    *is_array = true;
    return true;
}

bool minic_parser_build_function_declarator_type(MinicParser *parser,
                                                 MinicType return_type,
                                                 const MinicParsedFunctionDeclarator *declarator,
                                                 MinicType *declarator_type) {
    MinicParsedArraySuffix array_suffix;
    MinicSemanticSnapshot snapshot;
    MinicType function_type;
    size_t pointer_depth;

    if (parser == NULL || declarator == NULL || declarator_type == NULL ||
        declarator->parameter_count > MINIC_MAX_FUNCTION_PARAMETERS) {
        return false;
    }
    snapshot = minic_semantic_snapshot_capture(parser->program);
    if (!minic_c0_program_add_variadic_function_type(parser->program,
                                                     return_type,
                                                     declarator->parameter_types,
                                                     declarator->parameter_count,
                                                     declarator->is_variadic,
                                                     &function_type)) {
        (void)rollback_declarator_type_transaction(parser, &snapshot);
        return false;
    }

    pointer_depth = 0U;
    while (pointer_depth < declarator->pointer_depth) {
        unsigned int bit;

        if (!minic_type_pointer_to(function_type, &function_type)) {
            (void)rollback_declarator_type_transaction(parser, &snapshot);
            return false;
        }
        bit = 1U << pointer_depth;
        if ((declarator->pointer_const_qualifiers & bit) != 0U &&
            !minic_type_add_const(function_type, &function_type)) {
            (void)rollback_declarator_type_transaction(parser, &snapshot);
            return false;
        }
        if ((declarator->pointer_volatile_qualifiers & bit) != 0U &&
            !minic_type_add_volatile(function_type, &function_type)) {
            (void)rollback_declarator_type_transaction(parser, &snapshot);
            return false;
        }
        pointer_depth += 1U;
    }

    (void)memset(&array_suffix, 0, sizeof(array_suffix));
    (void)memcpy(array_suffix.bounds, declarator->array_bounds, sizeof(array_suffix.bounds));
    array_suffix.dimension_count = declarator->array_dimension_count;
    array_suffix.zero_length_mask = declarator->array_zero_length_mask;
    array_suffix.outermost_incomplete = declarator->array_outermost_incomplete;
    if (!materialize_array_suffix_type(
            parser, function_type, &array_suffix, NULL, NULL, NULL, &function_type)) {
        (void)rollback_declarator_type_transaction(parser, &snapshot);
        return false;
    }
    *declarator_type = function_type;
    return true;
}
