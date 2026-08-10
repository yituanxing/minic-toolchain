#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_function.c")
text = path.read_text()

# GNU attribute names live in their own syntactic namespace. Match source
# spelling rather than requiring the lexer to classify the name as an ordinary
# identifier.
anchor = '''static bool gnu_function_attribute_is_metadata(const MinicParser *parser) {
'''
helper = r'''static bool function_attribute_name_is(const MinicParser *parser, const char *name) {
    size_t name_length;

    if (parser == NULL || name == NULL || parser->current.kind == MINIC_TOKEN_EOF) {
        return false;
    }
    name_length = strlen(name);
    return minic_parser_span_length(parser->current.span) == name_length &&
           memcmp(parser->source + parser->current.span.begin.offset, name, name_length) == 0;
}

'''
if text.count(anchor) != 1:
    raise SystemExit(f"attribute-name matcher: expected one metadata helper anchor, found {text.count(anchor)}")
text = text.replace(anchor, helper + anchor, 1)

# cold is a non-ABI optimization hint.
old = '''           function_identifier_is(parser, "__always_inline__") ||
           function_identifier_is(parser, "noreturn") ||
'''
new = '''           function_identifier_is(parser, "__always_inline__") ||
           function_attribute_name_is(parser, "__cold__") ||
           function_attribute_name_is(parser, "cold") ||
           function_identifier_is(parser, "noreturn") ||
'''
if text.count(old) != 1:
    raise SystemExit(f"cold metadata classification: expected one anchor, found {text.count(old)}")
text = text.replace(old, new, 1)

# format(...) is diagnostic/checking metadata. Preserve the classification seam
# for a future format checker instead of silently treating every attribute as a
# generic hint.
old = '''           function_identifier_is(parser, "__warning__") ||
           function_identifier_is(parser, "warn_unused_result") ||
'''
new = '''           function_identifier_is(parser, "__warning__") ||
           function_attribute_name_is(parser, "format") ||
           function_attribute_name_is(parser, "__format__") ||
           function_identifier_is(parser, "warn_unused_result") ||
'''
if text.count(old) != 1:
    raise SystemExit(f"format diagnostic classification: expected one anchor, found {text.count(old)}")
text = text.replace(old, new, 1)

# Prefix attributes use the same explicit metadata+diagnostic policy as suffix
# attributes. Unknown and ABI/layout-affecting attributes remain hard errors.
old = '''            } else if (!gnu_function_attribute_is_metadata(parser)) {
                minic_parser_error(parser,
                                   "unsupported GNU prefix function attribute; semantic and "
                                   "ABI-affecting attributes must be implemented explicitly");
'''
new = '''            } else if (!gnu_function_attribute_is_metadata(parser) &&
                       !gnu_function_attribute_is_diagnostic(parser)) {
                minic_parser_error(parser,
                                   "unsupported GNU prefix function attribute; semantic and "
                                   "ABI-affecting attributes must be implemented explicitly");
'''
if text.count(old) != 1:
    raise SystemExit(f"prefix diagnostic routing: expected one classifier condition, found {text.count(old)}")
text = text.replace(old, new, 1)

# The older visibility-specific parser was greedy: it consumed every leading
# __attribute__ and rejected anything whose first name was not `visibility`.
# Make it a selective consumer. A probe recognizes only visibility(...) without
# mutating the real parser; any other attribute is left untouched for the shared
# GNU function-attribute parser that follows.
old = r'''    while (function_identifier_is(parser, "__attribute__")) {
        if (!minic_parser_advance(parser) ||
            !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '(' after __attribute__") ||
            !minic_parser_expect(parser, MINIC_TOKEN_LPAREN, "expected '((' after __attribute__")) {
            return false;
        }
        if (!function_identifier_is(parser, "visibility")) {
            minic_parser_error(parser, "unsupported GNU prefix function attribute");
            return false;
        }
'''
new = r'''    while (function_identifier_is(parser, "__attribute__")) {
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
'''
if text.count(old) != 1:
    raise SystemExit(f"selective visibility parser: expected one greedy visibility loop, found {text.count(old)}")
text = text.replace(old, new, 1)

path.write_text(text)
print("staged selective visibility-prefix parsing plus GNU format/cold classification")
