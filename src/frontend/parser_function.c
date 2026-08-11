#include "frontend/attribute.h"
#include "frontend/parser.h"
#include "frontend/parser_internal.h"

#include <limits.h>
#include <string.h>

static bool function_identifier_is(const MinicParser *parser, const char *name) {
    size_t name_length;

    if (parser == NULL || name == NULL || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        return false;
    }
    name_length = strlen(name);
    return minic_parser_span_length(parser->current.span) == name_length &&
           memcmp(parser->source + parser->current.span.begin.offset, name, name_length) == 0;
}

typedef struct MinicFunctionAttributeContext {
    bool allow_gnu_inline;
    bool is_internal;
    bool is_inline;
    const char *unsupported_message;
} MinicFunctionAttributeContext;

static bool function_attribute_class_is_parse_only(MinicAttributeClass semantic_class) {
    return semantic_class == MINIC_ATTRIBUTE_CLASS_INFORMATIONAL ||
           semantic_class == MINIC_ATTRIBUTE_CLASS_DIAGNOSTIC ||
           semantic_class == MINIC_ATTRIBUTE_CLASS_OPTIMIZATION ||
           semantic_class == MINIC_ATTRIBUTE_CLASS_CONTROL_FLOW;
}

static bool consume_function_attribute(MinicParser *parser,
                                       const MinicParsedAttribute *attribute,
                                       void *opaque_context) {
    const MinicFunctionAttributeContext *context;
    const MinicAttributeDescriptor *descriptor;

    if (parser == NULL || attribute == NULL || opaque_context == NULL) {
        return false;
    }
    context = (const MinicFunctionAttributeContext *)opaque_context;
    descriptor = attribute->descriptor;
    if (descriptor == NULL ||
        !minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_FUNCTION)) {
        minic_parser_error(parser, "%s", context->unsupported_message);
        return false;
    }

    if (descriptor->kind == MINIC_ATTRIBUTE_GNU_INLINE) {
        if (!context->allow_gnu_inline) {
            minic_parser_error(parser, "%s", context->unsupported_message);
            return false;
        }
        /* GNU inline changes external-inline linkage semantics. Linux's current
         * accepted placement is static inline, where this parse-only attribute
         * does not change externally visible linkage. */
        if (!context->is_internal || !context->is_inline) {
            minic_parser_error(parser,
                               "GNU gnu_inline requires explicit non-static inline semantics");
            return false;
        }
        return true;
    }

    if (!function_attribute_class_is_parse_only(descriptor->semantic_class)) {
        minic_parser_error(parser, "%s", context->unsupported_message);
        return false;
    }
    return true;
}

static bool parse_function_attribute_lists(MinicParser *parser,
                                           bool allow_gnu_inline,
                                           bool is_internal,
                                           bool is_inline,
                                           const char *unsupported_message) {
    MinicFunctionAttributeContext context;

    context.allow_gnu_inline = allow_gnu_inline;
    context.is_internal = is_internal;
    context.is_inline = is_inline;
    context.unsupported_message = unsupported_message;
    return minic_parser_parse_gnu_attribute_lists(parser, consume_function_attribute, &context);
}

static bool apply_function_attribute_list(MinicParser *parser,
                                          const MinicParsedAttributeList *attributes,
                                          bool allow_gnu_inline,
                                          bool is_internal,
                                          bool is_inline,
                                          const char *unsupported_message) {
    MinicFunctionAttributeContext context;
    size_t index;

    if (parser == NULL || attributes == NULL) {
        return false;
    }
    context.allow_gnu_inline = allow_gnu_inline;
    context.is_internal = is_internal;
    context.is_inline = is_inline;
    context.unsupported_message = unsupported_message;
    for (index = 0U; index < attributes->count; ++index) {
        if (!consume_function_attribute(parser, &attributes->values[index], &context)) {
            return false;
        }
    }
    return true;
}

static bool decode_deferred_section_argument(MinicParser *parser,
                                             const MinicParsedAttribute *attribute,
                                             char *buffer,
                                             size_t capacity,
                                             size_t *length,
                                             bool *has_section) {
    size_t cursor;
    size_t end;
    char parsed[256];
    size_t parsed_length;
    bool saw_literal;

    if (parser == NULL || attribute == NULL || buffer == NULL || length == NULL ||
        has_section == NULL || capacity == 0U || !attribute->has_arguments ||
        attribute->arguments_span.end.offset <= attribute->arguments_span.begin.offset + 1U) {
        return false;
    }
    cursor = attribute->arguments_span.begin.offset + 1U;
    end = attribute->arguments_span.end.offset - 1U;
    parsed_length = 0U;
    saw_literal = false;
    while (cursor < end) {
        while (cursor < end && (parser->source[cursor] == ' ' || parser->source[cursor] == '\t' ||
                                parser->source[cursor] == '\n' || parser->source[cursor] == '\r' ||
                                parser->source[cursor] == '\f' || parser->source[cursor] == '\v')) {
            cursor += 1U;
        }
        if (cursor >= end) {
            break;
        }
        if (parser->source[cursor] != '"') {
            minic_parser_error(parser,
                               "GNU section attribute requires concatenated string literals");
            return false;
        }
        saw_literal = true;
        cursor += 1U;
        while (cursor < end && parser->source[cursor] != '"') {
            if (parser->source[cursor] == '\\') {
                minic_parser_error(parser, "escaped GNU section names are not supported yet");
                return false;
            }
            if (parsed_length + 1U >= sizeof(parsed)) {
                minic_parser_error(parser, "GNU section name is too long");
                return false;
            }
            parsed[parsed_length++] = parser->source[cursor++];
        }
        if (cursor >= end || parser->source[cursor] != '"') {
            minic_parser_error(parser, "unterminated GNU section string");
            return false;
        }
        cursor += 1U;
    }
    if (!saw_literal || parsed_length == 0U || parsed_length + 1U > capacity) {
        minic_parser_error(parser, "invalid GNU section attribute argument");
        return false;
    }
    parsed[parsed_length] = '\0';
    if (*has_section) {
        if (*length != parsed_length || memcmp(buffer, parsed, parsed_length) != 0) {
            minic_parser_error(parser, "conflicting GNU section attributes");
            return false;
        }
        return true;
    }
    (void)memcpy(buffer, parsed, parsed_length + 1U);
    *length = parsed_length;
    *has_section = true;
    return true;
}

static bool apply_object_attribute_list(MinicParser *parser,
                                        const MinicParsedAttributeList *attributes,
                                        char *section_name,
                                        size_t section_capacity,
                                        size_t *section_name_length,
                                        bool *has_section,
                                        size_t *explicit_alignment) {
    size_t index;

    if (parser == NULL || attributes == NULL || section_name == NULL ||
        section_name_length == NULL || has_section == NULL || explicit_alignment == NULL) {
        return false;
    }
    for (index = 0U; index < attributes->count; ++index) {
        const MinicParsedAttribute *attribute;
        const MinicAttributeDescriptor *descriptor;

        attribute = &attributes->values[index];
        descriptor = attribute->descriptor;
        if (descriptor == NULL ||
            !minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_OBJECT)) {
            minic_parser_error(parser, "unsupported GNU object prefix attribute");
            return false;
        }
        if (function_attribute_class_is_parse_only(descriptor->semantic_class)) {
            continue;
        }
        if (descriptor->kind == MINIC_ATTRIBUTE_SECTION) {
            if (!decode_deferred_section_argument(parser,
                                                  attribute,
                                                  section_name,
                                                  section_capacity,
                                                  section_name_length,
                                                  has_section)) {
                return false;
            }
            continue;
        }
        if (descriptor->kind == MINIC_ATTRIBUTE_ALIGNED) {
            if (!minic_parser_apply_alignment_attribute(
                    parser, attribute, "object", explicit_alignment)) {
                return false;
            }
            continue;
        }
        minic_parser_error(parser,
                           "unsupported GNU object prefix attribute; symbol/layout attributes "
                           "require explicit object semantics");
        return false;
    }
    return true;
}

static bool section_attribute_token_is(const MinicParser *parser, const char *name) {
    size_t name_length;

    if (parser == NULL || name == NULL || parser->current.kind == MINIC_TOKEN_EOF) {
        return false;
    }
    name_length = strlen(name);
    return minic_parser_span_length(parser->current.span) == name_length &&
           memcmp(parser->source + parser->current.span.begin.offset, name, name_length) == 0;
}

bool minic_parser_parse_gnu_section_attribute(
    MinicParser *parser, char *buffer, size_t capacity, size_t *length, bool *has_section) {
    for (;;) {
        MinicParser probe;
        char parsed[256];
        size_t parsed_length;

        if (parser == NULL || buffer == NULL || length == NULL || has_section == NULL ||
            capacity == 0U) {
            return false;
        }
        if (!section_attribute_token_is(parser, "__attribute__")) {
            return true;
        }

        probe = *parser;
        if (!minic_parser_advance(&probe) || probe.current.kind != MINIC_TOKEN_LPAREN ||
            !minic_parser_advance(&probe) || probe.current.kind != MINIC_TOKEN_LPAREN ||
            !minic_parser_advance(&probe)) {
            return false;
        }
        if (!section_attribute_token_is(&probe, "section") &&
            !section_attribute_token_is(&probe, "__section__")) {
            return true;
        }

        if (!minic_parser_advance(parser) ||
            !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after __attribute__") ||
            !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '((' after __attribute__") ||
            (!section_attribute_token_is(parser, "section") &&
             !section_attribute_token_is(parser, "__section__")) ||
            !minic_parser_advance(parser) ||
            !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after section")) {
            return false;
        }

        parsed_length = 0U;
        if (parser->current.kind != MINIC_TOKEN_STRING_LITERAL) {
            minic_parser_error(parser, "GNU section attribute requires a string literal");
            return false;
        }
        while (parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
            size_t cursor;
            size_t end;

            if (parser->current.span.end.offset <= parser->current.span.begin.offset + 1U) {
                minic_parser_error(parser, "invalid GNU section string");
                return false;
            }
            cursor = parser->current.span.begin.offset + 1U;
            end = parser->current.span.end.offset - 1U;
            while (cursor < end) {
                if (parser->source[cursor] == '\\') {
                    minic_parser_error(parser, "escaped GNU section names are not supported yet");
                    return false;
                }
                if (parsed_length + 1U >= sizeof(parsed)) {
                    minic_parser_error(parser, "GNU section name is too long");
                    return false;
                }
                parsed[parsed_length++] = parser->source[cursor++];
            }
            if (!minic_parser_advance(parser)) {
                return false;
            }
        }
        if (parsed_length == 0U || parsed_length + 1U > capacity ||
            !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after section name") ||
            !minic_parser_expect(
                parser, MINIC_TOKEN_RPAREN, "expected ')' in GNU section attribute") ||
            !minic_parser_expect(
                parser, MINIC_TOKEN_RPAREN, "expected second ')' in GNU section attribute")) {
            return false;
        }
        parsed[parsed_length] = '\0';
        if (*has_section) {
            if (*length != parsed_length || memcmp(buffer, parsed, parsed_length) != 0) {
                minic_parser_error(parser, "conflicting GNU section attributes");
                return false;
            }
        } else {
            (void)memcpy(buffer, parsed, parsed_length + 1U);
            *length = parsed_length;
            *has_section = true;
        }
    }
}

bool minic_parser_parse_gnu_function_attributes(MinicParser *parser) {
    return parse_function_attribute_lists(
        parser,
        false,
        false,
        false,
        "unsupported GNU function attribute; ABI/layout-affecting and unknown attributes must be "
        "implemented explicitly");
}

bool minic_parser_parse_gnu_prefix_function_attributes(MinicParser *parser,
                                                       bool is_internal,
                                                       bool is_inline) {
    return parse_function_attribute_lists(
        parser,
        true,
        is_internal,
        is_inline,
        "unsupported GNU prefix function attribute; semantic and ABI-affecting attributes must be "
        "implemented explicitly");
}

static bool parse_gnu_function_asm_label(
    MinicParser *parser, char *buffer, size_t capacity, size_t *length, bool *has_label) {
    if (parser == NULL || buffer == NULL || length == NULL || has_label == NULL) {
        return false;
    }
    *length = 0U;
    *has_label = false;
    if (!function_identifier_is(parser, "__asm__") && !function_identifier_is(parser, "__asm")) {
        return true;
    }
    if (!minic_parser_advance(parser) ||
        !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after __asm__")) {
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_STRING_LITERAL) {
        minic_parser_error(parser, "GNU function asm label requires a string literal");
        return false;
    }
    while (parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
        size_t cursor;
        size_t end;

        if (parser->current.span.end.offset <= parser->current.span.begin.offset + 1U) {
            minic_parser_error(parser, "invalid GNU function asm label string");
            return false;
        }
        cursor = parser->current.span.begin.offset + 1U;
        end = parser->current.span.end.offset - 1U;
        while (cursor < end) {
            if (parser->source[cursor] == '\\') {
                minic_parser_error(parser, "escaped GNU function asm labels are not supported yet");
                return false;
            }
            if (*length + 1U >= capacity) {
                minic_parser_error(parser, "GNU function asm label is too long");
                return false;
            }
            buffer[*length] = parser->source[cursor];
            *length += 1U;
            cursor += 1U;
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    if (*length == 0U) {
        minic_parser_error(parser, "GNU function asm label cannot be empty");
        return false;
    }
    buffer[*length] = '\0';
    *has_label = true;
    return minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after GNU asm label");
}

static bool parse_gnu_visibility_name(MinicParser *parser, MinicSymbolVisibility *visibility) {
    MinicSourceSpan span;
    const char *value;
    size_t length;

    if (parser == NULL || visibility == NULL ||
        parser->current.kind != MINIC_TOKEN_STRING_LITERAL) {
        return false;
    }
    span = parser->current.span;
    if (span.end.offset <= span.begin.offset + 1U) {
        minic_parser_error(parser, "invalid GNU visibility string");
        return false;
    }
    value = parser->source + span.begin.offset + 1U;
    length = span.end.offset - span.begin.offset - 2U;
    if (length == 8U && memcmp(value, "internal", 8U) == 0) {
        *visibility = MINIC_SYMBOL_VISIBILITY_INTERNAL;
    } else if (length == 6U && memcmp(value, "hidden", 6U) == 0) {
        *visibility = MINIC_SYMBOL_VISIBILITY_HIDDEN;
    } else if (length == 9U && memcmp(value, "protected", 9U) == 0) {
        *visibility = MINIC_SYMBOL_VISIBILITY_PROTECTED;
    } else if (length == 7U && memcmp(value, "default", 7U) == 0) {
        *visibility = MINIC_SYMBOL_VISIBILITY_DEFAULT;
    } else {
        minic_parser_error(parser, "unsupported GNU visibility value");
        return false;
    }
    return minic_parser_advance(parser);
}

static bool parse_gnu_prefix_function_visibility(MinicParser *parser,
                                                 MinicSymbolVisibility *visibility,
                                                 bool *has_visibility) {
    if (parser == NULL || visibility == NULL || has_visibility == NULL) {
        return false;
    }
    *visibility = MINIC_SYMBOL_VISIBILITY_DEFAULT;
    *has_visibility = false;
    while (function_identifier_is(parser, "__attribute__")) {
        MinicParser probe = *parser;

        if (!minic_parser_advance(&probe) || probe.current.kind != MINIC_TOKEN_LPAREN ||
            !minic_parser_advance(&probe) || probe.current.kind != MINIC_TOKEN_LPAREN ||
            !minic_parser_advance(&probe)) {
            return false;
        }
        if (!function_identifier_is(&probe, "visibility")) {
            break;
        }
        if (!minic_parser_advance(parser) ||
            !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after __attribute__") ||
            !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '((' after __attribute__")) {
            return false;
        }
        if (!function_identifier_is(parser, "visibility")) {
            return false;
        }
        if (!minic_parser_advance(parser) ||
            !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after visibility") ||
            !parse_gnu_visibility_name(parser, visibility) ||
            !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' after visibility") ||
            !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' in GNU attribute") ||
            !minic_parser_expect(
                parser, MINIC_TOKEN_RPAREN, "expected second ')' in GNU attribute")) {
            return false;
        }
        *has_visibility = true;
    }
    return true;
}

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

static bool parse_function_pointer_parameter_declarator(MinicParser *parser,
                                                        MinicType return_type,
                                                        MinicSourceSpan *name_span,
                                                        bool *has_name,
                                                        MinicType *parameter_type,
                                                        bool require_name) {
    MinicParsedFunctionDeclarator declarator;

    if (parser == NULL || name_span == NULL || has_name == NULL || parameter_type == NULL ||
        !minic_parser_parse_parenthesized_function_declarator(
            parser, require_name, true, &declarator)) {
        return false;
    }
    if (declarator.is_variadic) {
        minic_parser_error(parser, "variadic function pointer parameters are not supported yet");
        return false;
    }
    if (!minic_parser_build_function_declarator_type(
            parser, return_type, &declarator, parameter_type)) {
        minic_parser_error(parser, "cannot build function pointer parameter type");
        return false;
    }
    *name_span = declarator.name_span;
    *has_name = declarator.has_name;
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

    for (;;) {
        MinicSourceSpan declarator_name_span;
        MinicType parameter_type;
        bool declarator_has_name;
        bool is_function_pointer_parameter;

        if (*parameter_count >= MINIC_MAX_FUNCTION_PARAMETERS) {
            minic_parser_error(parser, "parameter count exceeds compiler limit");
            return false;
        }
        if (!minic_parser_parse_type_name(parser, &parameter_type)) {
            return false;
        }
        (void)memset(&declarator_name_span, 0, sizeof(declarator_name_span));
        declarator_has_name = false;
        is_function_pointer_parameter = parser->current.kind == MINIC_TOKEN_LPAREN;
        if (is_function_pointer_parameter &&
            !parse_function_pointer_parameter_declarator(parser,
                                                         parameter_type,
                                                         &declarator_name_span,
                                                         &declarator_has_name,
                                                         &parameter_type,
                                                         require_names)) {
            return false;
        }
        if (!is_function_pointer_parameter && minic_type_is_void(parameter_type)) {
            if (*parameter_count == 0U && parser->current.kind == MINIC_TOKEN_RPAREN) {
                return true;
            }
            minic_parser_error(parser, "parameter type cannot be bare void");
            return false;
        }

        if (is_function_pointer_parameter) {
            if (declarator_has_name && parameter_name_spans != NULL) {
                parameter_name_spans[*parameter_count] = declarator_name_span;
            }
        } else if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
            if (parameter_name_spans != NULL) {
                parameter_name_spans[*parameter_count] = parser->current.span;
            }
            if (!minic_parser_advance(parser)) {
                return false;
            }
        } else if (require_names) {
            minic_parser_error(parser, "expected parameter name");
            return false;
        }

        parameter_types[*parameter_count] = parameter_type;
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

static bool parse_external_integer_array_definition(MinicParser *parser,
                                                    MinicType element_type,
                                                    MinicSourceSpan name_span) {
    MinicGlobalObjectId object_id;
    MinicGlobalObject *object;
    const MinicArrayType *array_type;
    MinicType declared_array_type;
    size_t element_count;
    size_t initializer_count;
    bool inferred_bound;
    bool definition_omits_bound;

    if (parser == NULL ||
        (!minic_type_is_integer(element_type) && !minic_type_is_pointer(element_type)) ||
        parser->current.kind != MINIC_TOKEN_LBRACKET) {
        minic_parser_error(parser,
                           "external array definition requires an integer or pointer element type");
        return false;
    }

    element_count = 0U;
    inferred_bound = false;
    definition_omits_bound = false;
    if (!minic_parser_advance(parser)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_RBRACKET) {
        inferred_bound = true;
        definition_omits_bound = true;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else if (!minic_parser_parse_fixed_array_bound(parser, &element_count)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
        minic_parser_error(parser,
                           "multi-dimensional external integer arrays are not supported yet");
        return false;
    }

    object_id = minic_parser_find_global_object(parser, name_span);
    if (object_id == MINIC_GLOBAL_OBJECT_INVALID) {
        if ((inferred_bound && !minic_c0_program_add_incomplete_array_type(
                                   parser->program, element_type, &declared_array_type)) ||
            (!inferred_bound &&
             !minic_c0_program_add_array_type(
                 parser->program, element_type, element_count, &declared_array_type)) ||
            !minic_c0_program_add_global_object(parser->program,
                                                parser->source + name_span.begin.offset,
                                                minic_parser_span_length(name_span),
                                                declared_array_type,
                                                false,
                                                minic_type_is_const(element_type),
                                                &object_id)) {
            minic_parser_error(parser, "cannot create external integer array definition");
            return false;
        }
    } else {
        object = &parser->program->global_objects[object_id];
        if (!object->is_extern || !minic_type_is_array(object->type)) {
            minic_parser_error(parser, "conflicting external integer array definition");
            return false;
        }
        array_type = minic_c0_program_array_type(parser->program, object->type.array_type_id);
        if (array_type == NULL || !minic_type_equal(array_type->element_type, element_type)) {
            minic_parser_error(parser, "external integer array definition type mismatch");
            return false;
        }
        if (array_type->element_count != 0U) {
            if (!inferred_bound && array_type->element_count != element_count) {
                minic_parser_error(parser, "external integer array bound mismatch");
                return false;
            }
            element_count = array_type->element_count;
            inferred_bound = false;
        } else if (!inferred_bound && !minic_c0_program_complete_array_type(
                                          parser->program, object->type, element_count)) {
            minic_parser_error(parser, "cannot complete external integer array bound");
            return false;
        }
    }

    object = &parser->program->global_objects[object_id];
    if (!minic_parser_expect(parser, MINIC_TOKEN_EQUAL, "expected '=' after external array")) {
        return false;
    }

    if (minic_type_is_pointer(element_type)) {
        if (!minic_parser_expect(
                parser, MINIC_TOKEN_LBRACE, "expected '{' in external pointer array initializer") ||
            !minic_c0_global_object_set_zero_initialized(parser->program, object_id)) {
            return false;
        }
        initializer_count = 0U;
        while (parser->current.kind != MINIC_TOKEN_RBRACE) {
            MinicGlobalObjectId target_id;
            bool has_relocation;

            if (!inferred_bound && initializer_count >= element_count) {
                minic_parser_error(parser, "too many external pointer array initializers");
                return false;
            }
            target_id = MINIC_GLOBAL_OBJECT_INVALID;
            has_relocation = false;
            if (parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
                MinicSourceSpan literal_span;
                MinicType literal_type;
                MinicType literal_pointer_type;
                const MinicArrayType *literal_array;

                if (!minic_parser_create_string_literal_object(
                        parser, &target_id, &literal_type, &literal_span)) {
                    return false;
                }
                literal_array =
                    minic_c0_program_array_type(parser->program, literal_type.array_type_id);
                if (literal_array == NULL || !minic_type_is_array(literal_type) ||
                    !minic_type_pointer_to(literal_array->element_type, &literal_pointer_type) ||
                    !minic_type_assignment_compatible(element_type, literal_pointer_type)) {
                    minic_parser_error(parser,
                                       "external pointer array string initializer type mismatch");
                    return false;
                }
                (void)literal_span;
                has_relocation = true;
            } else if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
                MinicType source_pointer_type;
                const MinicGlobalObject *target;

                target_id = minic_parser_find_global_object(parser, parser->current.span);
                target = target_id == MINIC_GLOBAL_OBJECT_INVALID
                             ? NULL
                             : minic_c0_program_global_object(parser->program, target_id);
                if (target == NULL) {
                    minic_parser_error(
                        parser, "external pointer array initializer requires a known object");
                    return false;
                }
                if (minic_type_is_array(target->type)) {
                    const MinicArrayType *target_array;

                    target_array =
                        minic_c0_program_array_type(parser->program, target->type.array_type_id);
                    if (target_array == NULL ||
                        !minic_type_pointer_to(target_array->element_type, &source_pointer_type)) {
                        minic_parser_error(parser, "cannot decay pointer array initializer object");
                        return false;
                    }
                } else if (!minic_type_pointer_to(target->type, &source_pointer_type)) {
                    minic_parser_error(parser,
                                       "cannot take address of pointer array initializer object");
                    return false;
                }
                if (!minic_type_assignment_compatible(element_type, source_pointer_type) ||
                    !minic_parser_advance(parser)) {
                    if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                        minic_parser_error(
                            parser, "external pointer array object initializer type mismatch");
                    }
                    return false;
                }
                has_relocation = true;
            } else {
                int64_t parsed;

                if (!minic_parser_parse_integer_constant_expression(parser, &parsed) ||
                    parsed != 0) {
                    if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                        minic_parser_error(
                            parser, "external pointer array scalar initializer must be null");
                    }
                    return false;
                }
            }
            if (has_relocation && !minic_c0_global_object_add_object_relocation(
                                      parser->program, object_id, initializer_count, target_id)) {
                minic_parser_error(parser, "cannot record external pointer array relocation");
                return false;
            }
            initializer_count += 1U;

            if (parser->current.kind == MINIC_TOKEN_COMMA) {
                if (!minic_parser_advance(parser)) {
                    return false;
                }
                if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                    break;
                }
            } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
                minic_parser_error(parser,
                                   "expected ',' or '}' in external pointer array initializer");
                return false;
            }
        }
        if (!minic_parser_expect(parser,
                                 MINIC_TOKEN_RBRACE,
                                 "expected '}' after external pointer array initializer")) {
            return false;
        }
        if (initializer_count == 0U) {
            minic_parser_error(parser, "external pointer array requires at least one initializer");
            return false;
        }
        if (inferred_bound) {
            element_count = initializer_count;
            if (!minic_c0_program_complete_array_type(
                    parser->program, object->type, element_count)) {
                minic_parser_error(parser, "cannot infer external pointer array bound");
                return false;
            }
        }
        object->is_extern = false;
        return minic_parser_expect(
            parser, MINIC_TOKEN_SEMICOLON, "expected ';' after external pointer array definition");
    }

    if (parser->current.kind == MINIC_TOKEN_STRING_LITERAL) {
        size_t string_count;

        if (!definition_omits_bound || !minic_type_is_char_integer(element_type) ||
            !inferred_bound ||
            !minic_parser_add_string_literal_initializer(parser, object_id, &string_count) ||
            !minic_c0_program_complete_array_type(parser->program, object->type, string_count)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "unsupported external string array definition");
            }
            return false;
        }
    } else {
        if (!minic_parser_expect(
                parser, MINIC_TOKEN_LBRACE, "expected '{' in external array initializer")) {
            return false;
        }
        initializer_count = 0U;
        while (parser->current.kind != MINIC_TOKEN_RBRACE) {
            int64_t parsed;

            if (!minic_parser_parse_integer_constant_expression(parser, &parsed)) {
                return false;
            }
            if (parsed < INT_MIN || parsed > INT_MAX) {
                minic_parser_error(parser,
                                   "external integer array initializer is out of supported range");
                return false;
            }
            if (!inferred_bound && initializer_count >= element_count) {
                minic_parser_error(parser, "too many external integer array initializers");
                return false;
            }
            if (!minic_c0_global_object_add_initializer(parser->program, object_id, (int)parsed)) {
                minic_parser_error(parser, "cannot record external integer array initializer");
                return false;
            }
            initializer_count += 1U;

            if (parser->current.kind == MINIC_TOKEN_COMMA) {
                if (!minic_parser_advance(parser)) {
                    return false;
                }
                if (parser->current.kind == MINIC_TOKEN_RBRACE) {
                    break;
                }
            } else if (parser->current.kind != MINIC_TOKEN_RBRACE) {
                minic_parser_error(parser, "expected ',' or '}' in external array initializer");
                return false;
            }
        }
        if (!minic_parser_expect(
                parser, MINIC_TOKEN_RBRACE, "expected '}' after external array initializer")) {
            return false;
        }
        if (initializer_count == 0U) {
            minic_parser_error(parser, "external integer array requires at least one initializer");
            return false;
        }
        if (inferred_bound) {
            element_count = initializer_count;
            if (!minic_c0_program_complete_array_type(
                    parser->program, object->type, element_count)) {
                minic_parser_error(parser, "cannot infer external integer array bound");
                return false;
            }
        }
    }

    object->is_extern = false;
    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, "expected ';' after external array definition");
}

static bool parse_external_object_definition(MinicParser *parser,
                                             MinicType object_type,
                                             MinicSourceSpan name_span) {
    MinicGlobalObjectId object_id;
    MinicGlobalObjectId target_id;
    MinicSourceSpan literal_span;
    MinicType literal_type;
    MinicType literal_pointer_type;
    const MinicArrayType *literal_array;
    MinicGlobalObject *object;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_EQUAL ||
        (!minic_type_is_integer(object_type) && !minic_type_is_pointer(object_type))) {
        minic_parser_error(parser, "unsupported external object definition");
        return false;
    }

    object_id = minic_parser_find_global_object(parser, name_span);
    if (object_id == MINIC_GLOBAL_OBJECT_INVALID) {
        if (!minic_c0_program_add_global_object(parser->program,
                                                parser->source + name_span.begin.offset,
                                                minic_parser_span_length(name_span),
                                                object_type,
                                                false,
                                                minic_type_is_const(object_type),
                                                &object_id)) {
            minic_parser_error(parser, "cannot create external object definition");
            return false;
        }
    } else {
        object = &parser->program->global_objects[object_id];
        if (!object->is_extern || !minic_type_equal(object->type, object_type) ||
            object->initializer_count != 0U || object->function_relocation_count != 0U ||
            object->object_relocation_count != 0U || object->is_zero_initialized) {
            minic_parser_error(parser, "conflicting external object definition");
            return false;
        }
        object->is_extern = false;
    }

    if (!minic_parser_advance(parser)) {
        return false;
    }
    if (minic_type_is_integer(object_type)) {
        int value;

        if (!minic_parser_parse_integer_value(parser, &value) ||
            !minic_c0_global_object_add_initializer(parser->program, object_id, value)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot record external integer initializer");
            }
            return false;
        }
        return minic_parser_expect(
            parser, MINIC_TOKEN_SEMICOLON, "expected ';' after external object definition");
    }

    if (parser->current.kind != MINIC_TOKEN_STRING_LITERAL ||
        !minic_parser_create_string_literal_object(
            parser, &target_id, &literal_type, &literal_span)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(parser,
                               "external pointer definition requires a string literal initializer");
        }
        return false;
    }
    literal_array = minic_c0_program_array_type(parser->program, literal_type.array_type_id);
    if (literal_array == NULL || !minic_type_is_array(literal_type) ||
        !minic_type_pointer_to(literal_array->element_type, &literal_pointer_type) ||
        !minic_type_assignment_compatible(object_type, literal_pointer_type) ||
        !minic_c0_global_object_set_zero_initialized(parser->program, object_id) ||
        !minic_c0_global_object_add_object_relocation(parser->program, object_id, 0U, target_id)) {
        minic_parser_error(parser, "external pointer initializer type mismatch");
        return false;
    }
    (void)literal_span;
    return minic_parser_expect(
        parser, MINIC_TOKEN_SEMICOLON, "expected ';' after external object definition");
}

static bool parse_visible_external_array(MinicParser *parser,
                                         MinicType element_type,
                                         MinicSourceSpan name_span,
                                         MinicSymbolVisibility visibility,
                                         bool has_visibility) {
    MinicParser probe;
    bool is_declaration;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_LBRACKET) {
        return false;
    }

    probe = *parser;
    if (!minic_parser_advance(&probe)) {
        return false;
    }
    while (probe.current.kind != MINIC_TOKEN_RBRACKET && probe.current.kind != MINIC_TOKEN_EOF) {
        if (!minic_parser_advance(&probe)) {
            return false;
        }
    }
    if (probe.current.kind != MINIC_TOKEN_RBRACKET || !minic_parser_advance(&probe)) {
        return false;
    }
    is_declaration = probe.current.kind == MINIC_TOKEN_SEMICOLON;

    if (is_declaration) {
        MinicGlobalObjectId object_id;
        MinicType array_type;
        size_t element_count;
        bool incomplete;

        if (!minic_parser_advance(parser)) {
            return false;
        }
        incomplete = parser->current.kind == MINIC_TOKEN_RBRACKET;
        if (incomplete) {
            if (!minic_c0_program_add_incomplete_array_type(
                    parser->program, element_type, &array_type) ||
                !minic_parser_advance(parser)) {
                minic_parser_error(parser, "cannot declare visible incomplete extern array");
                return false;
            }
        } else if (!minic_parser_parse_fixed_array_bound(parser, &element_count) ||
                   !minic_c0_program_add_array_type(
                       parser->program, element_type, element_count, &array_type)) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "cannot declare visible fixed extern array");
            }
            return false;
        }
        if (minic_parser_find_global_object(parser, name_span) != MINIC_GLOBAL_OBJECT_INVALID ||
            !minic_c0_program_add_global_object(parser->program,
                                                parser->source + name_span.begin.offset,
                                                minic_parser_span_length(name_span),
                                                array_type,
                                                false,
                                                minic_type_is_const(element_type),
                                                &object_id) ||
            !minic_c0_global_object_set_extern(parser->program, object_id) ||
            (has_visibility &&
             !minic_c0_global_object_set_visibility(parser->program, object_id, visibility))) {
            minic_parser_error(parser, "cannot record visible extern array declaration");
            return false;
        }
        return minic_parser_expect(
            parser, MINIC_TOKEN_SEMICOLON, "expected ';' after visible extern array declaration");
    }

    if (!parse_external_integer_array_definition(parser, element_type, name_span)) {
        return false;
    }
    if (has_visibility) {
        MinicGlobalObjectId object_id;

        object_id = minic_parser_find_global_object(parser, name_span);
        if (object_id == MINIC_GLOBAL_OBJECT_INVALID ||
            !minic_c0_global_object_set_visibility(parser->program, object_id, visibility)) {
            minic_parser_error(parser, "cannot record visible external array definition");
            return false;
        }
    }
    return true;
}

typedef struct MinicParsedDeclarationPrefix {
    MinicParsedAttributeList attributes;
    bool is_extern;
    bool is_static;
    bool is_register;
    bool is_inline;
} MinicParsedDeclarationPrefix;

static bool parse_declaration_prefix(MinicParser *parser,
                                     bool require_initial_static,
                                     MinicParsedDeclarationPrefix *prefix) {
    bool saw_storage_class;

    if (parser == NULL || prefix == NULL) {
        return false;
    }
    (void)memset(prefix, 0, sizeof(*prefix));
    saw_storage_class = false;

    for (;;) {
        if (function_identifier_is(parser, "register")) {
            if (saw_storage_class) {
                minic_parser_error(parser, "conflicting or duplicate declaration storage class");
                return false;
            }
            prefix->is_register = true;
            saw_storage_class = true;
            if (!minic_parser_advance(parser)) {
                return false;
            }
            continue;
        }
        if (parser->current.kind == MINIC_TOKEN_KW_STATIC) {
            if (saw_storage_class) {
                minic_parser_error(parser, "conflicting or duplicate declaration storage class");
                return false;
            }
            prefix->is_static = true;
            saw_storage_class = true;
            if (!minic_parser_advance(parser)) {
                return false;
            }
            continue;
        }
        if (parser->current.kind == MINIC_TOKEN_KW_EXTERN) {
            if (saw_storage_class) {
                minic_parser_error(parser, "conflicting or duplicate declaration storage class");
                return false;
            }
            prefix->is_extern = true;
            saw_storage_class = true;
            if (!minic_parser_advance(parser)) {
                return false;
            }
            continue;
        }
        if (parser->current.kind == MINIC_TOKEN_KW_INLINE) {
            if (prefix->is_inline) {
                minic_parser_error(parser, "duplicate inline declaration specifier");
                return false;
            }
            prefix->is_inline = true;
            if (!minic_parser_advance(parser)) {
                return false;
            }
            continue;
        }
        if (function_identifier_is(parser, "__attribute__")) {
            size_t old_offset = parser->current.span.begin.offset;

            if (!minic_parser_collect_gnu_attribute_lists(parser, &prefix->attributes)) {
                return false;
            }
            if (parser->current.span.begin.offset == old_offset) {
                minic_parser_error(parser, "internal error: GNU attribute prefix made no progress");
                return false;
            }
            continue;
        }
        break;
    }

    if (require_initial_static && !prefix->is_static) {
        minic_parser_error(parser, "expected keyword 'static'");
        return false;
    }
    return true;
}

static bool parse_function(MinicParser *parser, bool is_internal) {
    MinicSourceSpan name_span;
    MinicSourceSpan parameter_name_spans[MINIC_MAX_FUNCTION_PARAMETERS];
    MinicType parameter_types[MINIC_MAX_FUNCTION_PARAMETERS];
    MinicType base_type;
    MinicType return_type;
    MinicParsedAttributeList deferred_attributes;
    MinicParsedDeclarationPrefix declaration_prefix;
    MinicBlockId body_block;
    MinicFunctionId function_id;
    const MinicFunction *existing_function;
    MinicLocal parameter_local;
    MinicLocalId parameter_local_id;
    size_t parameter_count;
    size_t local_begin;
    size_t local_count;
    bool is_extern_declaration;
    bool is_function_pointer_object;
    bool is_inline;
    bool is_register_declaration;
    bool is_static_declaration;
    bool is_main;
    bool is_variadic;
    char assembler_name[256];
    size_t assembler_name_length;
    bool has_assembler_name;
    char section_name[256];
    size_t section_name_length;
    bool has_section;
    size_t object_explicit_alignment;
    MinicSymbolVisibility visibility;
    bool has_visibility;

    body_block = MINIC_BLOCK_INVALID;
    parameter_count = 0U;
    is_extern_declaration = false;
    is_function_pointer_object = false;
    is_inline = false;
    is_register_declaration = false;
    is_static_declaration = false;
    (void)memset(&deferred_attributes, 0, sizeof(deferred_attributes));
    (void)memset(&declaration_prefix, 0, sizeof(declaration_prefix));
    is_variadic = false;
    assembler_name_length = 0U;
    has_assembler_name = false;
    section_name_length = 0U;
    has_section = false;
    object_explicit_alignment = 0U;
    visibility = MINIC_SYMBOL_VISIBILITY_DEFAULT;
    has_visibility = false;
    (void)memset(assembler_name, 0, sizeof(assembler_name));
    (void)memset(section_name, 0, sizeof(section_name));
    (void)memset(parameter_name_spans, 0, sizeof(parameter_name_spans));
    (void)memset(parameter_types, 0, sizeof(parameter_types));
    if (!parse_gnu_prefix_function_visibility(parser, &visibility, &has_visibility) ||
        !parse_declaration_prefix(parser, is_internal, &declaration_prefix)) {
        return false;
    }
    is_extern_declaration = declaration_prefix.is_extern;
    is_static_declaration = declaration_prefix.is_static;
    is_register_declaration = declaration_prefix.is_register;
    is_internal = declaration_prefix.is_static;
    is_inline = declaration_prefix.is_inline;
    deferred_attributes = declaration_prefix.attributes;
    if (!minic_parser_parse_type_specifiers(parser, &base_type) ||
        !minic_parser_parse_gnu_section_attribute(
            parser, section_name, sizeof(section_name), &section_name_length, &has_section) ||
        !minic_parser_parse_pointer_declarator(parser, base_type, &return_type) ||
        !minic_parser_parse_gnu_section_attribute(
            parser, section_name, sizeof(section_name), &section_name_length, &has_section) ||
        !minic_parser_collect_gnu_attribute_lists(parser, &deferred_attributes)) {
        return false;
    }
    if (parser->current.kind == MINIC_TOKEN_IDENTIFIER) {
        name_span = parser->current.span;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } else if (parser->current.kind == MINIC_TOKEN_LPAREN) {
        MinicParser name_probe = *parser;

        if (!minic_parser_advance(&name_probe)) {
            return false;
        }
        if (is_extern_declaration && name_probe.current.kind == MINIC_TOKEN_STAR) {
            MinicParsedFunctionDeclarator declarator;

            if (!minic_parser_parse_parenthesized_function_declarator(
                    parser, true, true, &declarator)) {
                return false;
            }
            if (declarator.is_variadic) {
                minic_parser_error(
                    parser, "variadic extern function pointer objects are not supported yet");
                return false;
            }
            if (!minic_parser_build_function_declarator_type(
                    parser, return_type, &declarator, &return_type)) {
                minic_parser_error(parser, "cannot build extern function pointer object type");
                return false;
            }
            name_span = declarator.name_span;
            is_function_pointer_object = true;
        } else {
            if (!minic_parser_advance(parser)) {
                return false;
            }
            if (parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
                minic_parser_error(parser, "expected function name in parenthesized declarator");
                return false;
            }
            name_span = parser->current.span;
            if (!minic_parser_advance(parser) ||
                !minic_parser_expect(
                    parser, MINIC_TOKEN_RPAREN, "expected ')' after parenthesized function name")) {
                return false;
            }
        }
    } else {
        minic_parser_error(parser, "expected function or extern object name");
        return false;
    }

    if (is_register_declaration) {
        MinicFixedRegisterBindingId binding_id;

        if (is_inline || deferred_attributes.count != 0U || has_section || has_visibility ||
            is_function_pointer_object || parser->current.kind == MINIC_TOKEN_LPAREN ||
            (!minic_type_is_integer(return_type) && !minic_type_is_pointer(return_type))) {
            minic_parser_error(parser, "unsupported file-scope register declaration shape");
            return false;
        }
        if (!parse_gnu_function_asm_label(parser,
                                          assembler_name,
                                          sizeof(assembler_name),
                                          &assembler_name_length,
                                          &has_assembler_name) ||
            !has_assembler_name) {
            if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
                minic_parser_error(parser, "file-scope register declaration requires GNU asm name");
            }
            return false;
        }
        if (!minic_target_info_fixed_register_supported(
                parser->target_info, assembler_name, assembler_name_length)) {
            minic_parser_error(parser, "fixed register binding is not supported by this target");
            return false;
        }
        if (!minic_c0_program_add_fixed_register_binding(parser->program,
                                                         parser->source + name_span.begin.offset,
                                                         minic_parser_span_length(name_span),
                                                         return_type,
                                                         assembler_name,
                                                         assembler_name_length,
                                                         &binding_id)) {
            minic_parser_error(parser, "cannot record fixed register binding");
            return false;
        }
        (void)binding_id;
        return minic_parser_expect(
            parser, MINIC_TOKEN_SEMICOLON, "expected ';' after fixed register binding");
    }
    if (is_static_declaration && parser->current.kind != MINIC_TOKEN_LPAREN) {
        if (is_inline) {
            minic_parser_error(parser, "inline specifier requires a function declarator");
            return false;
        }
        if (!apply_object_attribute_list(parser,
                                         &deferred_attributes,
                                         section_name,
                                         sizeof(section_name),
                                         &section_name_length,
                                         &has_section,
                                         &object_explicit_alignment)) {
            return false;
        }
        if (has_section || has_visibility || object_explicit_alignment != 0U) {
            minic_parser_error(
                parser, "static object symbol/layout attributes require explicit object semantics");
            return false;
        }
        return minic_parser_parse_static_global_after_head(parser, return_type, name_span);
    }
    if (!is_internal &&
        (is_function_pointer_object || parser->current.kind != MINIC_TOKEN_LPAREN)) {
        if (is_inline) {
            minic_parser_error(parser, "inline specifier requires a function declarator");
            return false;
        }
        if (!apply_object_attribute_list(parser,
                                         &deferred_attributes,
                                         section_name,
                                         sizeof(section_name),
                                         &section_name_length,
                                         &has_section,
                                         &object_explicit_alignment)) {
            return false;
        }
        if (is_extern_declaration) {
            return minic_parser_parse_extern_global_after_head(parser,
                                                               base_type,
                                                               return_type,
                                                               name_span,
                                                               section_name,
                                                               section_name_length,
                                                               has_section,
                                                               object_explicit_alignment,
                                                               visibility,
                                                               has_visibility);
        }
        if (object_explicit_alignment != 0U) {
            minic_parser_error(
                parser, "GNU object alignment on a definition requires prior extern semantics");
            return false;
        }
        if (parser->current.kind == MINIC_TOKEN_LBRACKET) {
            return parse_visible_external_array(
                parser, return_type, name_span, visibility, has_visibility);
        }
        return parse_external_object_definition(parser, return_type, name_span);
    }
    if (!apply_function_attribute_list(
            parser,
            &deferred_attributes,
            true,
            is_internal,
            is_inline,
            "unsupported GNU prefix function attribute; semantic and ABI-affecting attributes must "
            "be implemented explicitly") ||
        !minic_parser_require_complete_object_type(
            parser, return_type, "incomplete record type requires pointer declarator")) {
        return false;
    }

    function_id = minic_parser_find_function(parser, name_span);
    is_main = minic_parser_span_length(name_span) == 4U &&
              memcmp(parser->source + name_span.begin.offset, "main", 4U) == 0;
    if (is_main && !minic_type_is_integer(return_type)) {
        minic_parser_error(parser, "main must return int");
        return false;
    }
    if (is_main && is_internal) {
        minic_parser_error(parser, "main cannot have internal linkage");
        return false;
    }

    if (!minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '('") ||
        !minic_parser_parse_parameter_list(
            parser, parameter_name_spans, parameter_types, &parameter_count, false, &is_variadic) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')'")) {
        return false;
    }
    if (!parse_gnu_function_asm_label(parser,
                                      assembler_name,
                                      sizeof(assembler_name),
                                      &assembler_name_length,
                                      &has_assembler_name) ||
        !minic_parser_parse_gnu_function_attributes(parser)) {
        return false;
    }
    if (is_main && (parameter_count != 0U || is_variadic)) {
        minic_parser_error(parser, "main parameters are not supported yet");
        return false;
    }

    if (function_id != MINIC_FUNCTION_INVALID) {
        existing_function = minic_c0_program_function(parser->program, function_id);
        if (!minic_parser_function_signature_matches(
                existing_function, return_type, parameter_types, parameter_count, is_variadic) ||
            (!existing_function->is_internal && is_internal)) {
            minic_parser_error(parser, "conflicting function declaration");
            return false;
        }
        if (existing_function->is_internal) {
            is_internal = true;
        }
    }

    if (parser->current.kind == MINIC_TOKEN_SEMICOLON) {
        if (function_id == MINIC_FUNCTION_INVALID) {
            if (!minic_c0_program_add_function(parser->program,
                                               parser->source + name_span.begin.offset,
                                               minic_parser_span_length(name_span),
                                               parser->program->local_count,
                                               0U,
                                               MINIC_BLOCK_INVALID,
                                               &function_id) ||
                !minic_c0_program_set_function_signature(
                    parser->program, function_id, return_type, parameter_types, parameter_count) ||
                !minic_c0_program_set_function_internal(
                    parser->program, function_id, is_internal) ||
                !minic_c0_program_set_function_variadic(
                    parser->program, function_id, is_variadic)) {
                minic_parser_error(parser, "out of memory while declaring function");
                return false;
            }
        }
        if (has_assembler_name &&
            !minic_c0_program_set_function_assembler_name(
                parser->program, function_id, assembler_name, assembler_name_length)) {
            minic_parser_error(parser, "conflicting or invalid GNU function asm label");
            return false;
        }
        if (has_visibility &&
            !minic_c0_program_set_function_visibility(parser->program, function_id, visibility)) {
            minic_parser_error(parser, "conflicting GNU function visibility");
            return false;
        }
        if (has_section && !minic_c0_program_set_function_section(
                               parser->program, function_id, section_name, section_name_length)) {
            minic_parser_error(parser, "conflicting or invalid GNU function section");
            return false;
        }
        return minic_parser_advance(parser);
    }
    if (!minic_type_is_integer(return_type) && !minic_type_is_void(return_type) &&
        !minic_type_is_pointer(return_type) && !minic_type_is_double(return_type) &&
        !minic_type_is_record(return_type)) {
        minic_parser_error(parser, "unsupported function return type");
        return false;
    }
    if (minic_type_is_record(return_type) &&
        !minic_parser_require_complete_object_type(
            parser, return_type, "function definition requires a complete record return type")) {
        return false;
    }
    if (parser->current.kind != MINIC_TOKEN_LBRACE) {
        minic_parser_error(parser, "expected ';' or '{' after function declarator");
        return false;
    }
    {
        size_t parameter_index;

        for (parameter_index = 0U; parameter_index < parameter_count; ++parameter_index) {
            if (minic_parser_span_length(parameter_name_spans[parameter_index]) == 0U) {
                minic_parser_error(parser, "function definition requires parameter names");
                return false;
            }
        }
    }
    if (function_id != MINIC_FUNCTION_INVALID) {
        existing_function = minic_c0_program_function(parser->program, function_id);
        if (existing_function == NULL || existing_function->is_defined) {
            minic_parser_error(parser, "duplicate function definition");
            return false;
        }
    }

    if (!minic_parser_advance(parser) ||
        !minic_c0_program_add_block(parser->program, &body_block)) {
        if (body_block == MINIC_BLOCK_INVALID) {
            minic_parser_error(parser, "out of memory while adding function body");
        }
        return false;
    }

    local_begin = parser->program->local_count;
    parser->local_begin = local_begin;
    if (!minic_parser_begin_scope(parser)) {
        return false;
    }
    {
        size_t parameter_index;

        for (parameter_index = 0U; parameter_index < parameter_count; ++parameter_index) {
            parameter_local.name_span = parameter_name_spans[parameter_index];
            parameter_local.type = parameter_types[parameter_index];
            parameter_local.element_count = 1U;
            parameter_local.storage_offset = 0U;
            parameter_local.is_array = false;
            if (minic_parser_find_local_in_current_scope(parser, parameter_local.name_span) !=
                MINIC_LOCAL_INVALID) {
                minic_parser_error(parser, "duplicate parameter name");
                return false;
            }
            if (!minic_c0_program_add_local(
                    parser->program, &parameter_local, &parameter_local_id)) {
                minic_parser_error(parser, "out of memory while adding parameter");
                return false;
            }
            if (!minic_parser_bind_local(parser, parameter_local.name_span, parameter_local_id)) {
                return false;
            }
        }
    }

    if (function_id == MINIC_FUNCTION_INVALID) {
        if (!minic_c0_program_add_function(parser->program,
                                           parser->source + name_span.begin.offset,
                                           minic_parser_span_length(name_span),
                                           local_begin,
                                           parameter_count,
                                           body_block,
                                           &function_id) ||
            !minic_c0_program_set_function_signature(
                parser->program, function_id, return_type, parameter_types, parameter_count) ||
            !minic_c0_program_set_function_internal(parser->program, function_id, is_internal) ||
            !minic_c0_program_set_function_variadic(parser->program, function_id, is_variadic)) {
            minic_parser_error(parser, "out of memory while adding function");
            return false;
        }
    } else if (!minic_c0_program_define_function(
                   parser->program, function_id, local_begin, body_block)) {
        minic_parser_error(parser, "cannot define previously declared function");
        return false;
    }
    if (has_assembler_name &&
        !minic_c0_program_set_function_assembler_name(
            parser->program, function_id, assembler_name, assembler_name_length)) {
        minic_parser_error(parser, "conflicting or invalid GNU function asm label");
        return false;
    }
    if (has_visibility &&
        !minic_c0_program_set_function_visibility(parser->program, function_id, visibility)) {
        minic_parser_error(parser, "conflicting GNU function visibility");
        return false;
    }
    if (has_section && !minic_c0_program_set_function_section(
                           parser->program, function_id, section_name, section_name_length)) {
        minic_parser_error(parser, "conflicting or invalid GNU function section");
        return false;
    }
    parser->current_function = function_id;
    if (is_main) {
        parser->program->entry_function = function_id;
        parser->program->body_block = body_block;
    }

    parser->current_block = body_block;
    while (parser->current.kind != MINIC_TOKEN_RBRACE) {
        if (parser->current.kind == MINIC_TOKEN_EOF) {
            minic_parser_error(parser, "expected '}' before end of file");
            return false;
        }
        if (!minic_parser_parse_statement(parser, true)) {
            return false;
        }
    }
    if ((!minic_type_is_pointer(return_type) && !minic_type_is_double(return_type) &&
         !minic_type_is_record(return_type) && !minic_parser_add_default_return(parser)) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RBRACE, "expected '}'")) {
        return false;
    }

    local_count = parser->program->local_count - local_begin;
    if (!minic_c0_program_finish_function(parser->program, function_id, local_count)) {
        minic_parser_error(parser, "invalid local range while finishing function");
        return false;
    }
    minic_parser_end_scope(parser);
    parser->current_function = MINIC_FUNCTION_INVALID;
    return true;
}

static bool top_level_is_gnu_extension_marker(const MinicParser *parser) {
    static const char marker[] = "__extension__";
    size_t length;

    if (parser == NULL || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        return false;
    }
    length = minic_parser_span_length(parser->current.span);
    return length == sizeof(marker) - 1U &&
           memcmp(parser->source + parser->current.span.begin.offset, marker, length) == 0;
}

static bool skip_top_level_gnu_extension_markers(MinicParser *parser) {
    while (top_level_is_gnu_extension_marker(parser)) {
        if (!minic_parser_advance(parser)) {
            return false;
        }
    }
    return true;
}

static bool enum_keyword_starts_standalone_declaration(MinicParser *parser, bool *is_standalone) {
    MinicParser probe;

    if (parser == NULL || is_standalone == NULL || parser->current.kind != MINIC_TOKEN_KW_ENUM) {
        return false;
    }
    probe = *parser;
    if (!minic_parser_advance(&probe)) {
        return false;
    }
    if (probe.current.kind == MINIC_TOKEN_LBRACE) {
        *is_standalone = true;
        return true;
    }
    if (probe.current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected enum tag or definition after enum keyword");
        return false;
    }
    if (!minic_parser_advance(&probe)) {
        return false;
    }
    *is_standalone =
        probe.current.kind == MINIC_TOKEN_SEMICOLON || probe.current.kind == MINIC_TOKEN_LBRACE;
    return true;
}

static bool record_keyword_starts_standalone_declaration(MinicParser *parser, bool *is_standalone) {
    MinicParser probe;
    size_t token_length;

    if (parser == NULL || is_standalone == NULL ||
        (parser->current.kind != MINIC_TOKEN_KW_STRUCT &&
         parser->current.kind != MINIC_TOKEN_KW_UNION)) {
        return false;
    }

    probe = *parser;
    if (!minic_parser_advance(&probe)) {
        return false;
    }
    if (probe.current.kind == MINIC_TOKEN_LBRACE) {
        *is_standalone = true;
        return true;
    }
    if (probe.current.kind != MINIC_TOKEN_IDENTIFIER) {
        minic_parser_error(parser, "expected record tag or definition after record keyword");
        return false;
    }

    token_length = minic_parser_span_length(probe.current.span);
    if (token_length == 13U &&
        memcmp(parser->source + probe.current.span.begin.offset, "__attribute__", 13U) == 0) {
        *is_standalone = true;
        return true;
    }

    if (!minic_parser_advance(&probe)) {
        return false;
    }
    *is_standalone =
        probe.current.kind == MINIC_TOKEN_SEMICOLON || probe.current.kind == MINIC_TOKEN_LBRACE;
    return true;
}

bool minic_parse_c0_program(const char *path,
                            const char *source,
                            size_t length,
                            MinicC0Program *program,
                            MinicDiagnostic *diagnostic) {
    MinicParser parser;
    bool success;

    (void)memset(&parser, 0, sizeof(parser));
    parser.path = path;
    parser.source = source;
    parser.diagnostic = diagnostic;
    parser.program = program;
    parser.target_info = minic_default_target_info();
    parser.current_block = MINIC_BLOCK_INVALID;
    parser.current_function = MINIC_FUNCTION_INVALID;
    parser.continue_target_statement = MINIC_STATEMENT_INVALID;
    minic_lexer_initialize(&parser.lexer, path, source, length);

    success = minic_parser_advance(&parser);
    while (success && parser.current.kind != MINIC_TOKEN_EOF) {
        success = skip_top_level_gnu_extension_markers(&parser);
        if (!success || parser.current.kind == MINIC_TOKEN_EOF) {
            break;
        }
        if (parser.current.kind == MINIC_TOKEN_SEMICOLON) {
            success = minic_parser_advance(&parser);
        } else if (parser.current.kind == MINIC_TOKEN_KW_STATIC_ASSERT) {
            success = minic_parser_parse_static_assert_declaration(&parser);
        } else if (parser.current.kind == MINIC_TOKEN_KW_TYPEDEF) {
            success = minic_parser_parse_typedef(&parser);
        } else if (parser.current.kind == MINIC_TOKEN_KW_EXTERN) {
            success = parse_function(&parser, false);
        } else if (parser.current.kind == MINIC_TOKEN_KW_INLINE) {
            success = parse_function(&parser, false);
        } else if (parser.current.kind == MINIC_TOKEN_KW_STATIC) {
            success = parse_function(&parser, true);
        } else if (parser.current.kind == MINIC_TOKEN_KW_STRUCT ||
                   parser.current.kind == MINIC_TOKEN_KW_UNION) {
            bool is_standalone;

            if (!record_keyword_starts_standalone_declaration(&parser, &is_standalone)) {
                success = false;
            } else if (is_standalone) {
                success = minic_parser_parse_record_definition(&parser);
            } else {
                success = parse_function(&parser, false);
            }
        } else if (parser.current.kind == MINIC_TOKEN_KW_ENUM) {
            bool is_standalone;

            if (!enum_keyword_starts_standalone_declaration(&parser, &is_standalone)) {
                success = false;
            } else if (is_standalone) {
                success = minic_parser_parse_enum_definition(&parser);
            } else {
                success = parse_function(&parser, false);
            }
        } else {
            success = parse_function(&parser, false);
        }
    }
    if (!success && diagnostic != NULL && diagnostic->message[0] == '\0') {
        minic_parser_error(&parser, "parser failed without diagnostic");
    }
    minic_parser_destroy_scopes(&parser);
    minic_parser_destroy_enum_constants(&parser);
    return success;
}
