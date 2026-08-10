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

static bool parse_attribute_arguments(MinicParser *parser, MinicParsedAttribute *attribute) {
    size_t depth;

    if (parser == NULL || attribute == NULL) {
        return false;
    }
    attribute->has_arguments = false;
    (void)memset(&attribute->arguments_span, 0, sizeof(attribute->arguments_span));
    if (parser->current.kind != MINIC_TOKEN_LPAREN) {
        return true;
    }

    attribute->has_arguments = true;
    attribute->arguments_span.begin = parser->current.span.begin;
    depth = 0U;
    do {
        if (parser->current.kind == MINIC_TOKEN_LPAREN) {
            depth += 1U;
        } else if (parser->current.kind == MINIC_TOKEN_RPAREN) {
            if (depth == 0U) {
                minic_parser_error(parser, "internal error: invalid GNU attribute argument depth");
                return false;
            }
            depth -= 1U;
        } else if (parser->current.kind == MINIC_TOKEN_EOF) {
            minic_parser_error(parser, "unterminated GNU attribute arguments");
            return false;
        }
        attribute->arguments_span.end = parser->current.span.end;
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } while (depth != 0U);
    return true;
}

bool minic_parser_parse_gnu_attribute_lists(MinicParser *parser,
                                            MinicParsedAttributeConsumer consumer,
                                            void *context) {
    if (parser == NULL || consumer == NULL) {
        return false;
    }

    while (parser_token_text_is(parser, parser->current, "__attribute__")) {
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
            if (!minic_parser_advance(parser) ||
                !parse_attribute_arguments(parser, &attribute) ||
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