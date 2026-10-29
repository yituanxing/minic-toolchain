#include "frontend/attribute.h"
#include "frontend/declaration_sema.h"
#include "frontend/parser_internal.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Function signatures, formal parameter grammar and parameter type adjustment. */

bool minic_parser_function_signature_matches(const MinicFunction *function,
                                             MinicType return_type,
                                             const MinicType *parameter_types,
                                             size_t parameter_count,
                                             bool is_variadic) {
    size_t parameter_index;

    if (function == NULL || !minic_type_equal(function->return_type, return_type) ||
        function->parameter_count != parameter_count || function->is_variadic != is_variadic) {
        return false;
    }
    for (parameter_index = 0U; parameter_index < parameter_count; ++parameter_index) {
        MinicType parameter_type;

        if (!minic_type_unqualified(parameter_types[parameter_index], &parameter_type) ||
            !minic_type_equal(function->parameter_types[parameter_index], parameter_type)) {
            return false;
        }
    }
    return true;
}

static bool calling_convention_scalar_type_supported(
    const MinicParser *parser, MinicType type, bool allow_void) {
    unsigned int width;
    if (allow_void && minic_type_is_void(type)) return true;
    if (minic_type_is_pointer(type) || minic_type_is_float(type) ||
        minic_type_is_double(type)) return true;
    return minic_type_is_integer(type) &&
           !minic_type_is_int128_integer(type) &&
           minic_target_info_integer_width(
               parser->target_info, parser->program, type, &width) &&
           width != 0U && width <= 64U;
}

bool minic_parser_calling_convention_fixed_scalar_supported(
    const MinicParser *parser, MinicFunctionCallingConvention convention,
    MinicType return_type, const MinicType *parameter_types,
    size_t parameter_count, bool is_variadic) {
    size_t i;
    if (parser == NULL || parser->target_info == NULL || is_variadic ||
        parameter_count > MINIC_MAX_FUNCTION_PARAMETERS ||
        (parameter_count != 0U && parameter_types == NULL) ||
        !minic_target_info_function_calling_convention_scalar_call_supported(
            parser->target_info, convention) ||
        !calling_convention_scalar_type_supported(parser, return_type, true))
        return false;
    for (i = 0U; i < parameter_count; ++i)
        if (!calling_convention_scalar_type_supported(
                parser, parameter_types[i], false))
            return false;
    return true;
}

static bool parse_function_pointer_parameter_declarator(MinicParser *parser,
                                                        MinicType return_type,
                                                        MinicSourceSpan *name_span,
                                                        bool *has_name,
                                                        MinicType *parameter_type,
                                                        bool require_name) {
    MinicParsedFunctionDeclarator declarator;

    if (parser == NULL || name_span == NULL || has_name == NULL || parameter_type == NULL ||
        !minic_parser_parse_parenthesized_function_declarator(
            parser, require_name, false, &declarator)) {
        return false;
    }
    if (!minic_parser_build_function_declarator_type(
            parser, return_type, &declarator, parameter_type)) {
        minic_parser_error(parser, "cannot build function pointer parameter type");
        return false;
    }
    for (size_t attribute_index = 0U;
         attribute_index < declarator.attributes.count; ++attribute_index) {
        const MinicParsedAttribute *attribute =
            &declarator.attributes.values[attribute_index];
        const MinicAttributeDescriptor *descriptor = attribute->descriptor;
        if (descriptor != NULL && descriptor->kind == MINIC_ATTRIBUTE_MS_ABI &&
            minic_target_info_function_pointer_calling_convention_declaration_supported(
                parser->target_info, MINIC_CALLING_CONVENTION_MS_X64) &&
            minic_c0_program_function_pointer_with_calling_convention(
                parser->program, *parameter_type,
                MINIC_CALLING_CONVENTION_MS_X64, parameter_type))
            continue;
        if (descriptor != NULL &&
            minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_FUNCTION) &&
            (descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_INFORMATIONAL ||
             descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_DIAGNOSTIC ||
             descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_OPTIMIZATION ||
             descriptor->semantic_class == MINIC_ATTRIBUTE_CLASS_CONTROL_FLOW))
            continue;
        minic_parser_error(parser,
            "unsupported GNU ABI attribute on function pointer parameter");
        return false;
    }
    *name_span = declarator.name_span;
    *has_name = declarator.has_name;
    return true;
}

static bool parameter_starts_parenthesized_pointer_to_array(const MinicParser *parser) {
    MinicParser probe;
    size_t pointer_depth;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_LPAREN) {
        return false;
    }
    probe = *parser;
    if (!minic_parser_advance(&probe)) {
        return false;
    }
    pointer_depth = 0U;
    while (probe.current.kind == MINIC_TOKEN_STAR) {
        pointer_depth += 1U;
        if (!minic_parser_advance(&probe)) {
            return false;
        }
        while (probe.current.kind == MINIC_TOKEN_KW_CONST ||
               probe.current.kind == MINIC_TOKEN_KW_VOLATILE ||
               minic_parser_current_identifier_lexeme_equals_cstr(&probe, "restrict") ||
               minic_parser_current_identifier_lexeme_equals_cstr(&probe, "__restrict") ||
               minic_parser_current_identifier_lexeme_equals_cstr(&probe, "__restrict__")) {
            if (!minic_parser_advance(&probe)) {
                return false;
            }
        }
    }
    if (pointer_depth == 0U || probe.current.kind != MINIC_TOKEN_IDENTIFIER ||
        !minic_parser_advance(&probe) || probe.current.kind != MINIC_TOKEN_RPAREN ||
        !minic_parser_advance(&probe)) {
        return false;
    }
    return probe.current.kind == MINIC_TOKEN_LBRACKET;
}

static bool parameter_array_modifier_is_restrict(const MinicParser *parser) {
    return minic_parser_current_identifier_lexeme_equals_cstr(parser, "restrict") ||
           minic_parser_current_identifier_lexeme_equals_cstr(parser, "__restrict") ||
           minic_parser_current_identifier_lexeme_equals_cstr(parser, "__restrict__");
}

/* Array-parameter outer bounds are discarded during array-to-pointer
 * adjustment. A lone name of a preceding integer parameter is a valid
 * C99 variably modified bound (e.g. regexec's [__restrict __nmatch]).
 * Do not turn that name into a false local-expression lookup; equally,
 * do not accept an undeclared or noninteger bound without validation. */
static bool consume_prior_parameter_bound(MinicParser *parser,
                                          const MinicSourceSpan *prior_names,
                                          const MinicType *prior_types,
                                          size_t prior_count,
                                          bool *consumed) {
    MinicParser probe;
    size_t i;
    size_t length;

    if (parser == NULL || consumed == NULL) return false;
    *consumed = false;
    if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) return true;
    probe = *parser;
    if (!minic_parser_advance(&probe)) return false;
    if (probe.current.kind != MINIC_TOKEN_RBRACKET) return true;
    length = minic_parser_span_length(parser->current.span);
    for (i = 0U; i < prior_count; ++i) {
        if (minic_type_is_integer(prior_types[i]) &&
            minic_parser_span_length(prior_names[i]) == length &&
            length != 0U &&
            memcmp(parser->source + prior_names[i].begin.offset,
                   parser->source + parser->current.span.begin.offset,
                   length) == 0) {
            *consumed = true;
            return minic_parser_advance(parser) &&
                   minic_parser_expect(parser, MINIC_TOKEN_RBRACKET, "expected ']'");
        }
    }
    /* A sole enum constant such as BusyBox's meminfo[MI_MAX] is a C ICE,
     * not a prior-parameter VLA bound. Leave it for the existing fixed-bound
     * parser, which verifies the enum value and outer array adjustment.
     * Do not accept unknown identifiers or shadow the preceding parameter. */
    if (minic_parser_find_enum_constant(parser, parser->current.span) !=
        MINIC_ENUMERATOR_INVALID) {
        return true;
    }
    minic_parser_error(parser, "array parameter bound must name a preceding integer parameter");
    return false;
}

static bool adjust_array_parameter_type(MinicParser *parser,
                                        MinicType *parameter_type,
                                        size_t array_type_begin,
                                        const MinicSourceSpan *prior_names,
                                        const MinicType *prior_types,
                                        size_t prior_count) {
    const MinicArrayType *outer_array;
    MinicType adjusted_type;
    MinicType declared_array_type;
    MinicType pointee_type;
    bool is_array;

    if (parser == NULL || parameter_type == NULL) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        bool saw_static;
        bool has_outer_bound;
        bool consumed_named_bound;
        size_t ignored_outer_bound;

        saw_static = false;
        has_outer_bound = false;
        consumed_named_bound = false;
        if (!minic_parser_advance(parser)) {
            return false;
        }
        while (parser->current.kind == MINIC_TOKEN_KW_STATIC ||
               parser->current.kind == MINIC_TOKEN_KW_CONST ||
               parser->current.kind == MINIC_TOKEN_KW_VOLATILE ||
               parameter_array_modifier_is_restrict(parser)) {
            if (parser->current.kind == MINIC_TOKEN_KW_STATIC) {
                saw_static = true;
            }
            if (!minic_parser_advance(parser)) {
                return false;
            }
        }
        if (parser->current.kind == MINIC_TOKEN_RBRACKET) {
            if (saw_static) {
                minic_parser_error(parser, "static array parameter requires a bound");
                return false;
            }
            if (!minic_parser_advance(parser)) {
                return false;
            }
        } else {
            if (!consume_prior_parameter_bound(parser, prior_names, prior_types,
                                               prior_count, &consumed_named_bound)) {
                return false;
            }
            if (!consumed_named_bound &&
                !minic_parser_parse_fixed_array_bound(parser, &ignored_outer_bound)) {
                return false;
            }
            has_outer_bound = true;
        }
        (void)has_outer_bound;
        (void)ignored_outer_bound;

        pointee_type = *parameter_type;
        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            if (!minic_parser_parse_array_declarator_suffix(
                    parser, *parameter_type, false, &declared_array_type, &is_array) ||
                !is_array || !minic_type_is_array(declared_array_type)) {
                if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                    minic_parser_error(parser, "cannot parse nested array parameter declarator");
                }
                return false;
            }
            pointee_type = declared_array_type;
        }
        if (!minic_type_pointer_to(pointee_type, &adjusted_type)) {
            minic_parser_error(parser, "cannot adjust array parameter to pointer type");
            return false;
        }
        *parameter_type = adjusted_type;
        return true;
    }
    if (!minic_type_is_array(*parameter_type)) {
        return true;
    }
    declared_array_type = *parameter_type;
    outer_array = minic_c0_program_array_type(parser->program, declared_array_type.array_type_id);
    if (outer_array == NULL) {
        minic_parser_error(parser, "cannot resolve array parameter declarator");
        return false;
    }
    pointee_type = outer_array->element_type;
    if (!minic_type_pointer_to(pointee_type, &adjusted_type)) {
        minic_parser_error(parser, "cannot adjust array parameter to pointer type");
        return false;
    }
    *parameter_type = adjusted_type;

    /* C array parameters adjust to pointers. If the outer array descriptor was
       materialized only while parsing this parameter, the adjusted pointer no
       longer owns it. Retire exactly that transient outer descriptor; descriptors
       that predate the parameter (for example typedef-owned arrays) remain owned,
       and nested element-array descriptors remain referenced by pointee_type. */
    if ((size_t)declared_array_type.array_type_id >= array_type_begin) {
        if ((size_t)declared_array_type.array_type_id + 1U != parser->program->array_type_count ||
            !minic_c0_program_discard_last_array_type(parser->program, declared_array_type)) {
            minic_parser_error(parser, "cannot retire adjusted array parameter descriptor");
            return false;
        }
    }
    return true;
}

static bool adjust_function_parameter_type(MinicParser *parser, MinicType *parameter_type) {
    MinicType adjusted_type;

    if (parser == NULL || parameter_type == NULL) {
        return false;
    }
    if (!minic_type_is_function(*parameter_type)) {
        return true;
    }
    if (!minic_type_pointer_to(*parameter_type, &adjusted_type)) {
        minic_parser_error(parser, "cannot adjust function parameter to pointer type");
        return false;
    }
    *parameter_type = adjusted_type;
    return true;
}

static bool parameter_attribute_class_is_parse_only(MinicAttributeClass semantic_class) {
    return semantic_class == MINIC_ATTRIBUTE_CLASS_INFORMATIONAL ||
           semantic_class == MINIC_ATTRIBUTE_CLASS_DIAGNOSTIC ||
           semantic_class == MINIC_ATTRIBUTE_CLASS_OPTIMIZATION ||
           semantic_class == MINIC_ATTRIBUTE_CLASS_CONTROL_FLOW;
}

static bool consume_parameter_declarator_attribute(MinicParser *parser,
                                                   const MinicParsedAttribute *attribute,
                                                   void *opaque_context) {
    const MinicAttributeDescriptor *descriptor;

    const bool *function_parameter = (const bool *)opaque_context;
    if (parser == NULL || attribute == NULL) {
        return false;
    }
    descriptor = attribute->descriptor;
    /* GNU function-pointer declarator parameters may carry attributes of
     * the referenced FUNCTION (e.g. libsepol callback format(printf,...)).
     * Those are informational/diagnostic, not an attribute on the pointer
     * OBJECT. Never admit a function attribute for an ordinary scalar. */
    if (descriptor == NULL ||
        (!minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_OBJECT) &&
         !(function_parameter != NULL && *function_parameter &&
           minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_FUNCTION)))) {
        minic_parser_error(parser, "unsupported GNU parameter declarator attribute");
        return false;
    }
    if (parameter_attribute_class_is_parse_only(descriptor->semantic_class)) {
        return true;
    }
    minic_parser_error(
        parser, "GNU parameter declarator attribute requires explicit language/layout semantics");
    return false;
}

static bool apply_parameter_declarator_attribute_list(MinicParser *parser,
                                                      const MinicParsedAttributeList *attributes,
                                                      const bool *function_parameter) {
    size_t index;

    if (parser == NULL || attributes == NULL) {
        return false;
    }
    for (index = 0U; index < attributes->count; ++index) {
        if (!consume_parameter_declarator_attribute(
                parser, &attributes->values[index], (void *)function_parameter)) {
            return false;
        }
    }
    return true;
}

bool minic_parser_parse_parameter_list(MinicParser *parser,
                                       MinicSourceSpan *parameter_name_spans,
                                       MinicType *parameter_types,
                                       size_t *parameter_count,
                                       bool require_names,
                                       bool *is_variadic) {
    if (parser == NULL || parameter_types == NULL || parameter_count == NULL ||
        is_variadic == NULL) {
        return false;
    }
    *is_variadic = false;
    if (parser->current.kind == MINIC_TOKEN_RPAREN) {
        return true;
    }
    if (parser->current.kind == MINIC_TOKEN_ELLIPSIS) {
        minic_parser_error(parser, "ellipsis requires at least one fixed parameter");
        return false;
    }

    /* Parameter lists can be parsed with parameter_name_spans == NULL
     * (nested function-pointer and prototype declarators). Keep a local
     * name table so C99 outer array bounds still see earlier parameters. */
    MinicSourceSpan prior_parameter_names[MINIC_MAX_FUNCTION_PARAMETERS];

    (void)memset(prior_parameter_names, 0, sizeof(prior_parameter_names));
    for (;;) {
        MinicSourceSpan declarator_name_span;
        MinicType parameter_type;
        MinicParsedAttributeList leading_attributes;
        MinicParsedAttributeList post_type_attributes;
        bool declarator_has_name;
        bool declarator_name_is_array;
        bool declarator_name_array_inferred;
        bool is_function_pointer_parameter;
        bool is_pointer_to_array_parameter;
        size_t declarator_name_array_count;
        size_t parameter_array_type_begin;

        leading_attributes.count = 0U;
        post_type_attributes.count = 0U;
        if (*parameter_count >= MINIC_MAX_FUNCTION_PARAMETERS) {
            minic_parser_error(parser, "parameter count exceeds compiler limit");
            return false;
        }
        if (!minic_parser_collect_gnu_attribute_lists(parser, &leading_attributes)) {
            return false;
        }
        /* C permits register on function parameters.  It affects storage-class
           semantics only; the ABI parameter type is unchanged. */
        if (minic_parser_current_identifier_lexeme_equals_cstr(parser, "register") && !minic_parser_advance(parser)) {
            return false;
        }
        parameter_array_type_begin = parser->program->array_type_count;
        if (!minic_parser_parse_type_name_preserving_incomplete(parser, &parameter_type) ||
            !minic_parser_collect_gnu_attribute_lists(parser, &post_type_attributes) ||
            !minic_parser_parse_pointer_declarator(parser, parameter_type, &parameter_type)) {
            return false;
        }
        (void)memset(&declarator_name_span, 0, sizeof(declarator_name_span));
        declarator_has_name = false;
        declarator_name_is_array = false;
        declarator_name_array_inferred = false;
        declarator_name_array_count = 0U;
        is_pointer_to_array_parameter =
            parameter_starts_parenthesized_pointer_to_array(parser);
        is_function_pointer_parameter =
            parser->current.kind == MINIC_TOKEN_LPAREN && !is_pointer_to_array_parameter;
        if (is_pointer_to_array_parameter) {
            if (!minic_parser_parse_parenthesized_pointer_to_array_declarator(
                    parser,
                    parameter_type,
                    &declarator_name_span,
                    &parameter_type,
                    &declarator_name_is_array,
                    &declarator_name_array_inferred,
                    &declarator_name_array_count)) {
                return false;
            }
            if (declarator_name_is_array) {
                MinicType adjusted_parameter_type;

                if (!minic_type_pointer_to(parameter_type, &adjusted_parameter_type)) {
                    minic_parser_error(
                        parser, "cannot adjust parenthesized array parameter to pointer type");
                    return false;
                }
                parameter_type = adjusted_parameter_type;
            }
            (void)declarator_name_array_inferred;
            (void)declarator_name_array_count;
            declarator_has_name = true;
        } else if (is_function_pointer_parameter &&
                   !parse_function_pointer_parameter_declarator(parser,
                                                                parameter_type,
                                                                &declarator_name_span,
                                                                &declarator_has_name,
                                                                &parameter_type,
                                                                require_names)) {
            return false;
        }
        if (is_function_pointer_parameter || is_pointer_to_array_parameter) {
            if (declarator_has_name && parameter_name_spans != NULL) {
                parameter_name_spans[*parameter_count] = declarator_name_span;
            }
        } else if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
            MinicSourceSpan parameter_name_span;

            parameter_name_span = parser->current.span;
            declarator_name_span = parameter_name_span;
            declarator_has_name = true;
            if (parameter_name_spans != NULL) {
                parameter_name_spans[*parameter_count] = parameter_name_span;
            }
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind == MINIC_TOKEN_LPAREN) {
                MinicParsedFunctionDeclarator direct_function;

                (void)memset(&direct_function, 0, sizeof(direct_function));
                direct_function.name_span = parameter_name_span;
                direct_function.has_name = true;
                if (!minic_parser_parse_function_parameter_suffix(parser, &direct_function) ||
                    !minic_parser_build_function_declarator_type(
                        parser, parameter_type, &direct_function, &parameter_type)) {
                    if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                        minic_parser_error(parser,
                                           "cannot build direct function parameter declarator");
                    }
                    return false;
                }
            }
        } else if (require_names) {
            minic_parser_error(parser, "expected parameter name");
            return false;
        }

        if (!is_function_pointer_parameter && !is_pointer_to_array_parameter &&
            minic_type_is_void(parameter_type)) {
            if (!declarator_has_name && *parameter_count == 0U &&
                parser->current.kind == MINIC_TOKEN_RPAREN) {
                return true;
            }
            minic_parser_error(parser, "parameter type cannot be bare void");
            return false;
        }

        if (!apply_parameter_declarator_attribute_list(
                parser, &leading_attributes, &is_function_pointer_parameter) ||
            !apply_parameter_declarator_attribute_list(
                parser, &post_type_attributes, &is_function_pointer_parameter) ||
            !minic_parser_parse_gnu_attribute_lists(
                parser, consume_parameter_declarator_attribute,
                &is_function_pointer_parameter)) {
            return false;
        }

        if (!is_function_pointer_parameter && !is_pointer_to_array_parameter &&
            !adjust_array_parameter_type(
                parser, &parameter_type, parameter_array_type_begin,
                prior_parameter_names, parameter_types, *parameter_count)) {
            return false;
        }
        if (!adjust_function_parameter_type(parser, &parameter_type)) {
            return false;
        }

        parameter_types[*parameter_count] = parameter_type;
        if (declarator_has_name) {
            prior_parameter_names[*parameter_count] = declarator_name_span;
        }
        *parameter_count += 1U;
        if (parser->current.kind != MINIC_TOKEN_COMMA) {
            return true;
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_ELLIPSIS) {
            *is_variadic = true;
            return minic_parser_advance(parser);
        }
    }
}
