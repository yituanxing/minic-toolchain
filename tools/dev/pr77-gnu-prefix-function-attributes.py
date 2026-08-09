#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_function.c")
text = path.read_text()

old = '''           function_identifier_is(parser, "__malloc__") ||
           function_identifier_is(parser, "noreturn") ||
'''
new = '''           function_identifier_is(parser, "__malloc__") ||
           function_identifier_is(parser, "__unused__") ||
           function_identifier_is(parser, "__no_instrument_function__") ||
           function_identifier_is(parser, "noreturn") ||
'''
if text.count(old) != 1:
    raise SystemExit(f"metadata attribute anchor: expected one match, found {text.count(old)}")
text = text.replace(old, new, 1)

anchor = '''static bool parse_gnu_predeclarator_function_attributes(MinicParser *parser) {
    return parse_gnu_function_attributes(parser);
}

'''
helper = r'''static bool parse_gnu_prefix_function_attributes(MinicParser *parser,
                                                 bool is_internal,
                                                 bool is_inline) {
    while (function_identifier_is(parser, "__attribute__")) {
        if (!minic_parser_advance(parser) ||
            !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after __attribute__") ||
            !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '((' after __attribute__")) {
            return false;
        }
        while (parser->current.kind != MINIC_TOKEN_RPAREN) {
            bool is_gnu_inline = function_identifier_is(parser, "__gnu_inline__");

            if (is_gnu_inline) {
                /* GNU inline changes external-inline linkage semantics.  Linux's first
                 * real use here is static inline, where the attribute does not alter
                 * externally visible linkage.  Keep other placements rejected until
                 * inline semantics are represented explicitly. */
                if (!is_internal || !is_inline) {
                    minic_parser_error(
                        parser,
                        "GNU gnu_inline requires explicit non-static inline semantics");
                    return false;
                }
            } else if (!gnu_function_attribute_is_metadata(parser)) {
                minic_parser_error(parser,
                                   "unsupported GNU prefix function attribute; semantic and "
                                   "ABI-affecting attributes must be implemented explicitly");
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
                minic_parser_error(parser, "expected ',' or ')' in GNU prefix attribute list");
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
    raise SystemExit(f"predeclarator helper anchor: expected one match, found {text.count(anchor)}")
text = text.replace(anchor, anchor + helper, 1)

old = '''    if (!minic_parser_parse_type_name(parser, &return_type)) {
        return false;
    }
    if (!parse_gnu_predeclarator_function_attributes(parser)) {
'''
new = '''    if (!parse_gnu_prefix_function_attributes(parser, is_internal, is_inline)) {
        return false;
    }
    if (!minic_parser_parse_type_name(parser, &return_type)) {
        return false;
    }
    if (!parse_gnu_predeclarator_function_attributes(parser)) {
'''
if text.count(old) != 1:
    raise SystemExit(f"function prefix call anchor: expected one match, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))
print("staged GNU prefix function attributes unused,no_instrument and static-inline gnu_inline")
