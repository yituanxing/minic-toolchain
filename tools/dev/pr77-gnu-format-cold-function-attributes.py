#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_function.c")
text = path.read_text()

# GNU attribute names occupy their own syntactic namespace. Some valid names can
# also be lexer keywords/extensions, so attribute recognition must compare the
# token spelling rather than require MINIC_TOKEN_IDENTIFIER.
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

# `cold` is an optimization hint and belongs with non-ABI metadata.
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

# `format(...)` is diagnostic/checking metadata. Keep it with error/warning and
# warn_unused_result rather than treating it as a generic optimization hint.
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

# Prefix and suffix attributes now share the same explicit metadata+diagnostic
# policy. Unknown or ABI/layout-affecting attributes remain hard errors.
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
path.write_text(text.replace(old, new, 1))
print("staged token-spelling GNU attribute matching, format diagnostics and cold optimization hint")
