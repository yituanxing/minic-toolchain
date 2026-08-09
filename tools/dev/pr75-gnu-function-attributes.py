#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_function.c")
text = path.read_text()

anchor = '''static bool function_signature_matches(const MinicFunction *function,\n'''
helper = r'''static bool function_identifier_is(const MinicParser *parser, const char *name) {
    size_t name_length;

    if (parser == NULL || name == NULL || parser->current.kind != MINIC_TOKEN_IDENTIFIER) {
        return false;
    }
    name_length = strlen(name);
    return minic_parser_span_length(parser->current.span) == name_length &&
           memcmp(parser->source + parser->current.span.begin.offset, name, name_length) == 0;
}

static bool gnu_function_attribute_is_metadata(const MinicParser *parser) {
    return function_identifier_is(parser, "__nothrow__") ||
           function_identifier_is(parser, "__leaf__") ||
           function_identifier_is(parser, "__nonnull__") ||
           function_identifier_is(parser, "__access__") ||
           function_identifier_is(parser, "__pure__");
}

static bool parse_gnu_attribute_arguments(MinicParser *parser) {
    size_t depth;

    if (parser->current.kind != MINIC_TOKEN_LPAREN) {
        return true;
    }
    depth = 0U;
    do {
        if (parser->current.kind == MINIC_TOKEN_LPAREN) {
            depth += 1U;
        } else if (parser->current.kind == MINIC_TOKEN_RPAREN) {
            depth -= 1U;
        } else if (parser->current.kind == MINIC_TOKEN_EOF) {
            minic_parser_error(parser, "unterminated GNU attribute arguments");
            return false;
        }
        if (!minic_parser_advance(parser)) {
            return false;
        }
    } while (depth != 0U);
    return true;
}

static bool parse_gnu_function_attributes(MinicParser *parser) {
    while (function_identifier_is(parser, "__attribute__")) {
        if (!minic_parser_advance(parser) ||
            !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after __attribute__") ||
            !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '((' after __attribute__")) {
            return false;
        }

        while (parser->current.kind != MINIC_TOKEN_RPAREN) {
            if (!gnu_function_attribute_is_metadata(parser)) {
                minic_parser_error(parser,
                                   "unsupported GNU function attribute; ABI/layout-affecting and "
                                   "unknown attributes must be implemented explicitly");
                return false;
            }
            if (!minic_parser_advance(parser) || !parse_gnu_attribute_arguments(parser)) {
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
            !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected second ')' in GNU attribute")) {
            return false;
        }
    }
    return true;
}

'''
if text.count(anchor) != 1:
    raise SystemExit(f"function helper anchor: expected 1 match, found {text.count(anchor)}")
text = text.replace(anchor, helper + anchor, 1)

old = '''    if (!minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '('") ||
        !minic_parser_parse_parameter_list(
            parser, parameter_name_spans, parameter_types, &parameter_count, false, &is_variadic) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')'")) {
        return false;
    }
'''
new = '''    if (!minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '('") ||
        !minic_parser_parse_parameter_list(
            parser, parameter_name_spans, parameter_types, &parameter_count, false, &is_variadic) ||
        !minic_parser_expect(parser, MINIC_TOKEN_RPAREN, "expected ')'")) {
        return false;
    }
    if (!parse_gnu_function_attributes(parser)) {
        return false;
    }
'''
if text.count(old) != 1:
    raise SystemExit(f"function declarator attribute anchor: expected 1 match, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))
print("staged non-ABI GNU function attributes: nothrow, leaf, nonnull, access, pure")
