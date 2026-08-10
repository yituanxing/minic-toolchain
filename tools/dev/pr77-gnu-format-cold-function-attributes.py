#!/usr/bin/env python3
from pathlib import Path

path = Path("src/frontend/parser_function.c")
text = path.read_text()

# `cold` is an optimization hint and belongs with non-ABI metadata.
old = '''           function_identifier_is(parser, "__always_inline__") ||
           function_identifier_is(parser, "noreturn") ||
'''
new = '''           function_identifier_is(parser, "__always_inline__") ||
           function_identifier_is(parser, "__cold__") ||
           function_identifier_is(parser, "cold") ||
           function_identifier_is(parser, "noreturn") ||
'''
if text.count(old) != 1:
    raise SystemExit(f"cold metadata classification: expected one anchor, found {text.count(old)}")
text = text.replace(old, new, 1)

# `format(...)` is diagnostic/checking metadata. Keep it with error/warning and
# warn_unused_result rather than pretending it is a generic optimization hint.
old = '''           function_identifier_is(parser, "__warning__") ||
           function_identifier_is(parser, "warn_unused_result") ||
'''
new = '''           function_identifier_is(parser, "__warning__") ||
           function_identifier_is(parser, "format") ||
           function_identifier_is(parser, "__format__") ||
           function_identifier_is(parser, "warn_unused_result") ||
'''
if text.count(old) != 1:
    raise SystemExit(f"format diagnostic classification: expected one anchor, found {text.count(old)}")
text = text.replace(old, new, 1)

# Prefix GNU attributes historically only consulted the metadata classifier,
# while suffix attributes already consult metadata + diagnostic. Make the
# prefix path use the same explicit classification policy; unknown or ABI/layout
# affecting attributes are still rejected.
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
print("staged GNU format as diagnostic metadata, cold as optimization hint, and shared prefix classification")
