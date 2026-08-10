#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_function.c")
text = path.read_text()

# TEMPORARY discovery diagnostic: remove once the real attribute path is known.
include_anchor = "#include <string.h>\n"
if text.count(include_anchor) != 1:
    raise SystemExit(f"attribute debug include: expected one string.h include, found {text.count(include_anchor)}")
text = text.replace(include_anchor, "#include <stdio.h>\n#include <string.h>\n", 1)

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

old = '''        while (parser->current.kind != MINIC_TOKEN_RPAREN) {
            bool is_gnu_inline = function_identifier_is(parser, "__gnu_inline__");

            if (is_gnu_inline) {
'''
new = '''        while (parser->current.kind != MINIC_TOKEN_RPAREN) {
            bool is_gnu_inline = function_identifier_is(parser, "__gnu_inline__");
            bool is_known_nonabi_prefix = function_attribute_name_is(parser, "format") ||
                                          function_attribute_name_is(parser, "__format__") ||
                                          function_attribute_name_is(parser, "cold") ||
                                          function_attribute_name_is(parser, "__cold__");

            if (is_gnu_inline) {
'''
if text.count(old) != 1:
    raise SystemExit(f"prefix known attribute setup: expected one loop anchor, found {text.count(old)}")
text = text.replace(old, new, 1)

old = '''            } else if (!gnu_function_attribute_is_metadata(parser)) {
                minic_parser_error(parser,
                                   "unsupported GNU prefix function attribute; semantic and "
                                   "ABI-affecting attributes must be implemented explicitly");
'''
new = '''            } else if (!is_known_nonabi_prefix &&
                       !gnu_function_attribute_is_metadata(parser) &&
                       !gnu_function_attribute_is_diagnostic(parser)) {
                size_t attribute_length = minic_parser_span_length(parser->current.span);

                fprintf(stderr,
                        "ATTR_DEBUG kind=%d len=%zu spelling=%.*s\\n",
                        (int)parser->current.kind,
                        attribute_length,
                        (int)attribute_length,
                        parser->source + parser->current.span.begin.offset);
                minic_parser_error(parser,
                                   "unsupported GNU prefix function attribute; semantic and "
                                   "ABI-affecting attributes must be implemented explicitly");
'''
if text.count(old) != 1:
    raise SystemExit(f"prefix diagnostic routing: expected one classifier condition, found {text.count(old)}")
text = text.replace(old, new, 1)
path.write_text(text)

# TEMPORARY: print every remaining site carrying the same diagnostic so the CI
# log exposes duplicate/generated parser paths after all previous staging.
lines = text.splitlines()
needle = "unsupported GNU prefix function attribute"
matches = [index for index, line in enumerate(lines) if needle in line]
print(f"ATTR_SITE_COUNT={len(matches)}")
for index in matches:
    begin = max(0, index - 12)
    end = min(len(lines), index + 8)
    print(f"ATTR_SITE line={index + 1} context_begin={begin + 1}")
    for source_index in range(begin, end):
        print(f"ATTR_SRC {source_index + 1}: {lines[source_index]}")
print("staged temporary prefix-attribute path diagnostics")
