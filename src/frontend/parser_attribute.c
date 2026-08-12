#include "frontend/parser_internal.h"

#include <string.h>

static bool parser_token_text_is(const MinicParser *parser, MinicToken token, const char *text) {
    size_t text_length;

    if (parser == NULL || text == NULL) {
        return false;
    }
    text_length = strlen(text);
    return minic_parser_span_length(token.span) == text_length &&
           memcmp(parser->source + token.span.begin.offset, text, text_length) == 0;
}

static const MinicAttributeDescriptor *attribute_descriptor_for_token(const MinicParser *parser,
                                                                      MinicToken token) {
    size_t name_length;

    if (parser == NULL || token.kind == MINIC_TOKEN_EOF) {
        return NULL;
    }
    name_length = minic_parser_span_length(token.span);
    return minic_attribute_lookup(parser->source + token.span.begin.offset, name_length);
}

static bool validate_attribute_argument_count(MinicParser *parser,
                                              const MinicParsedAttribute *attribute,
                                              size_t argument_count) {
    if (parser == NULL || attribute == NULL) {
        return false;
    }
    if (attribute->descriptor != NULL &&
        !minic_attribute_argument_count_allowed(attribute->descriptor, argument_count)) {
        minic_parser_error(parser, "GNU attribute has an invalid number of arguments");
        return false;
    }
    return true;
}

static bool parse_attribute_arguments(MinicParser *parser, MinicParsedAttribute *attribute) {
    size_t depth;
    size_t argument_count;
    bool saw_argument_token;

    if (parser == NULL || attribute == NULL) {
        return false;
    }
    attribute->has_arguments = false;
    (void)memset(&attribute->arguments_span, 0, sizeof(attribute->arguments_span));
    if (parser->current.kind != MINIC_TOKEN_LPAREN) {
        return validate_attribute_argument_count(parser, attribute, 0U);
    }

    attribute->has_arguments = true;
    attribute->arguments_span.begin = parser->current.span.begin;
    depth = 0U;
    argument_count = 0U;
    saw_argument_token = false;
    do {
        MinicTokenKind kind;

        kind = parser->current.kind;
        if (kind == MINIC_TOKEN_LPAREN) {
            if (depth == 1U) {
                saw_argument_token = true;
            }
            depth += 1U;
        } else if (kind == MINIC_TOKEN_RPAREN) {
            if (depth == 0U) {
                minic_parser_error(parser, "internal error: invalid GNU attribute argument depth");
                return false;
            }
            if (depth == 1U) {
                if (saw_argument_token) {
                    argument_count += 1U;
                } else if (argument_count != 0U) {
                    minic_parser_error(parser, "GNU attribute argument list cannot end with ','");
                    return false;
                }
            }
            depth -= 1U;
        } else if (kind == MINIC_TOKEN_EOF) {
            minic_parser_error(parser, "unterminated GNU attribute arguments");
            return false;
        } else if (depth == 1U && kind == MINIC_TOKEN_COMMA) {
            if (!saw_argument_token) {
                minic_parser_error(parser, "GNU attribute argument cannot be empty");
                return false;
            }
            argument_count += 1U;
            saw_argument_token = false;
        } else if (depth != 0U) {
            saw_argument_token = true;
        }
        attribute->arguments_span.end = parser->current.span.end;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } while (depth != 0U);
    return validate_attribute_argument_count(parser, attribute, argument_count);
}

bool minic_parser_parse_gnu_attribute_lists(MinicParser *parser,
                                            MinicParsedAttributeConsumer consumer,
                                            void *context) {
    if (parser == NULL || consumer == NULL) {
        return false;
    }

    while (parser_token_text_is(parser, parser->current, "__attribute__") ||
           parser_token_text_is(parser, parser->current, "__attribute")) {
        if (!minic_parser_advance(parser) ||
            !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after __attribute__") ||
            !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '((' after __attribute__")) {
            return false;
        }

        while (parser->current.kind != MINIC_TOKEN_RPAREN) {
            MinicParsedAttribute attribute;

            if (parser->current.kind == MINIC_TOKEN_EOF) {
                minic_parser_error(parser, "unterminated GNU attribute list");
                return false;
            }
            (void)memset(&attribute, 0, sizeof(attribute));
            attribute.name_span = parser->current.span;
            attribute.descriptor = attribute_descriptor_for_token(parser, parser->current);
            if (!minic_parser_advance(parser) || !parse_attribute_arguments(parser, &attribute) ||
                !consumer(parser, &attribute, context)) {
                return false;
            }

            if (parser->current.kind == MINIC_TOKEN_COMMA) {
                if (!minic_parser_advance(parser)) {
                    return false;
                }
            } else if (parser->current.kind != MINIC_TOKEN_RPAREN) {
                minic_parser_error(parser, "expected ',' or ')' in GNU attribute list");
                return false;
            }
        }

        if (!minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')' in GNU attribute") ||
            !minic_parser_expect(
                parser, MINIC_TOKEN_RPAREN, "expected second ')' in GNU attribute")) {
            return false;
        }
    }
    return true;
}

static bool collect_parsed_attribute(MinicParser *parser,
                                     const MinicParsedAttribute *attribute,
                                     void *opaque_context) {
    MinicParsedAttributeList *attributes;

    if (parser == NULL || attribute == NULL || opaque_context == NULL) {
        return false;
    }
    attributes = (MinicParsedAttributeList *)opaque_context;
    if (attributes->count >= MINIC_MAX_PARSED_ATTRIBUTES) {
        minic_parser_error(parser, "too many GNU attributes on one declaration");
        return false;
    }
    attributes->values[attributes->count] = *attribute;
    attributes->count += 1U;
    return true;
}

bool minic_parser_collect_gnu_attribute_lists(MinicParser *parser,
                                              MinicParsedAttributeList *attributes) {
    if (parser == NULL || attributes == NULL) {
        return false;
    }
    return minic_parser_parse_gnu_attribute_lists(parser, collect_parsed_attribute, attributes);
}

bool minic_parser_apply_section_attribute(MinicParser *parser,
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

typedef struct MinicObjectAttributeContext {
    char *section_name;
    size_t section_capacity;
    size_t *section_name_length;
    bool *has_section;
    size_t *explicit_alignment;
} MinicObjectAttributeContext;

static bool object_attribute_class_is_parse_only(MinicAttributeClass semantic_class) {
    return semantic_class == MINIC_ATTRIBUTE_CLASS_INFORMATIONAL ||
           semantic_class == MINIC_ATTRIBUTE_CLASS_DIAGNOSTIC ||
           semantic_class == MINIC_ATTRIBUTE_CLASS_OPTIMIZATION ||
           semantic_class == MINIC_ATTRIBUTE_CLASS_CONTROL_FLOW;
}

static bool consume_object_attribute(MinicParser *parser,
                                     const MinicParsedAttribute *attribute,
                                     void *opaque_context) {
    const MinicAttributeDescriptor *descriptor;
    MinicObjectAttributeContext *context;

    if (parser == NULL || attribute == NULL || opaque_context == NULL) {
        return false;
    }
    context = (MinicObjectAttributeContext *)opaque_context;
    descriptor = attribute->descriptor;
    if (descriptor == NULL ||
        !minic_attribute_allowed_on(descriptor, MINIC_ATTRIBUTE_TARGET_OBJECT)) {
        minic_parser_error(parser, "unsupported GNU object attribute");
        return false;
    }
    if (object_attribute_class_is_parse_only(descriptor->semantic_class)) {
        return true;
    }
    if (descriptor->kind == MINIC_ATTRIBUTE_SECTION) {
        return minic_parser_apply_section_attribute(parser,
                                                    attribute,
                                                    context->section_name,
                                                    context->section_capacity,
                                                    context->section_name_length,
                                                    context->has_section);
    }
    if (descriptor->kind == MINIC_ATTRIBUTE_ALIGNED) {
        return minic_parser_apply_alignment_attribute(
            parser, attribute, "object", context->explicit_alignment);
    }
    minic_parser_error(parser,
                       "unsupported GNU object attribute; symbol/layout attributes require "
                       "explicit object semantics");
    return false;
}

static bool initialize_object_attribute_context(MinicObjectAttributeContext *context,
                                                char *section_name,
                                                size_t section_capacity,
                                                size_t *section_name_length,
                                                bool *has_section,
                                                size_t *explicit_alignment) {
    if (context == NULL || section_name == NULL || section_capacity == 0U ||
        section_name_length == NULL || has_section == NULL || explicit_alignment == NULL) {
        return false;
    }
    context->section_name = section_name;
    context->section_capacity = section_capacity;
    context->section_name_length = section_name_length;
    context->has_section = has_section;
    context->explicit_alignment = explicit_alignment;
    return true;
}

bool minic_parser_apply_object_attribute_list(MinicParser *parser,
                                              const MinicParsedAttributeList *attributes,
                                              char *section_name,
                                              size_t section_capacity,
                                              size_t *section_name_length,
                                              bool *has_section,
                                              size_t *explicit_alignment) {
    MinicObjectAttributeContext context;
    size_t index;

    if (parser == NULL || attributes == NULL ||
        !initialize_object_attribute_context(&context,
                                             section_name,
                                             section_capacity,
                                             section_name_length,
                                             has_section,
                                             explicit_alignment)) {
        return false;
    }
    for (index = 0U; index < attributes->count; ++index) {
        if (!consume_object_attribute(parser, &attributes->values[index], &context)) {
            return false;
        }
    }
    return true;
}

bool minic_parser_parse_gnu_object_attribute_lists(MinicParser *parser,
                                                   char *section_name,
                                                   size_t section_capacity,
                                                   size_t *section_name_length,
                                                   bool *has_section,
                                                   size_t *explicit_alignment) {
    MinicObjectAttributeContext context;

    if (parser == NULL || !initialize_object_attribute_context(&context,
                                                               section_name,
                                                               section_capacity,
                                                               section_name_length,
                                                               has_section,
                                                               explicit_alignment)) {
        return false;
    }
    return minic_parser_parse_gnu_attribute_lists(parser, consume_object_attribute, &context);
}

bool minic_parser_apply_alignment_attribute(MinicParser *parser,
                                            const MinicParsedAttribute *attribute,
                                            const char *subject,
                                            size_t *explicit_alignment) {
    MinicParser probe;
    MinicExpressionId expression_id;
    const MinicExpression *expression;
    MinicConstValue constant_value;
    int64_t parsed_alignment;
    size_t alignment;

    if (parser == NULL || attribute == NULL || subject == NULL || explicit_alignment == NULL ||
        !attribute->has_arguments ||
        attribute->arguments_span.end.offset <= attribute->arguments_span.begin.offset + 1U) {
        return false;
    }
    probe = *parser;
    minic_lexer_initialize(&probe.lexer, parser->path, parser->source, parser->lexer.length);
    probe.lexer.cursor = attribute->arguments_span.begin.offset + 1U;
    probe.lexer.line = attribute->arguments_span.begin.line;
    probe.lexer.column = attribute->arguments_span.begin.column + 1U;
    if (!minic_parser_advance(&probe) ||
        !minic_parser_parse_expression(&probe, &expression_id, 0U)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(
                parser, "GNU %s alignment requires one integer constant expression", subject);
        }
        return false;
    }
    expression = minic_c0_program_expression(parser->program, expression_id);
    if (probe.current.kind != MINIC_TOKEN_RPAREN ||
        probe.current.span.end.offset != attribute->arguments_span.end.offset ||
        expression == NULL || !minic_type_is_integer(expression->type) ||
        !minic_const_eval_integer(
            parser->program, parser->target_info, expression_id, &constant_value) ||
        !minic_const_value_as_int64(
            parser->program, parser->target_info, &constant_value, &parsed_alignment)) {
        if (parser->diagnostic != NULL && parser->diagnostic->message[0] == '\0') {
            minic_parser_error(
                parser, "GNU %s alignment requires one integer constant expression", subject);
        }
        return false;
    }
    if (parsed_alignment <= 0 || (uint64_t)parsed_alignment > (uint64_t)SIZE_MAX) {
        minic_parser_error(
            parser, "GNU %s alignment must be a positive target-size value", subject);
        return false;
    }
    alignment = (size_t)parsed_alignment;
    if ((alignment & (alignment - 1U)) != 0U) {
        minic_parser_error(parser, "GNU %s alignment must be a power of two", subject);
        return false;
    }
    if (alignment > *explicit_alignment) {
        *explicit_alignment = alignment;
    }
    return true;
}
