#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_function.c")
text = path.read_text()
old = '''static bool gnu_function_attribute_is_diagnostic(const MinicParser *parser) {
    return function_identifier_is(parser, "error") ||
           function_identifier_is(parser, "__error__") ||
           function_identifier_is(parser, "warning") ||
           function_identifier_is(parser, "__warning__");
}
'''
new = '''static bool gnu_function_attribute_is_diagnostic(const MinicParser *parser) {
    return function_identifier_is(parser, "error") ||
           function_identifier_is(parser, "__error__") ||
           function_identifier_is(parser, "warning") ||
           function_identifier_is(parser, "__warning__") ||
           function_identifier_is(parser, "warn_unused_result") ||
           function_identifier_is(parser, "__warn_unused_result__");
}
'''
if text.count(old) != 1:
    raise SystemExit(f"warn_unused_result classification: expected one diagnostic helper, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))
print("staged GNU warn_unused_result as an explicit diagnostic function attribute")
