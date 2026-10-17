#!/usr/bin/env python3
"""A/B-only: avoid copying 133KiB MinicParser for enum/record top-level lookahead.

This is distinct from the already-integrated parser_type.c GNU int128
lookahead fix (2026-10-16). Only lexer/token are read, the original Parser
stays untouched until parse commits. No grammar, failure-policy, or AST edits.
Exact anchors must hold. Apply only in a disposable CI checkout, not at runtime.
"""
from pathlib import Path

source = Path("src/frontend/parser_function.c")
original = source.read_text()
start = "static bool enum_keyword_starts_standalone_declaration("
end = "static bool finalize_tentative_incomplete_arrays("
assert original.count(start) == 1 and original.count(end) == 1
prefix, remaining = original.split(start, 1)
body, suffix = remaining.split(end, 1)
assert body.count("static bool record_keyword_starts_standalone_declaration(") == 1

def replace_exact(text, old, new, count):
    assert text.count(old) == count, (repr(old), text.count(old), count)
    return text.replace(old, new)

body = replace_exact(body,
    "    MinicParser probe;\n",
    "    MinicLexer probe_lexer;\n    MinicToken probe_token;\n", 2)
body = replace_exact(body,
    "    probe = *parser;\n",
    "    probe_lexer = parser->lexer;\n", 2)
body = replace_exact(body,
    "minic_parser_advance(&probe)",
    "minic_lexer_next(&probe_lexer, &probe_token, parser->diagnostic)", 4)
body = replace_exact(body, "probe.current.kind", "probe_token.kind", 8)
body = replace_exact(body, "probe.current.span", "probe_token.span", 2)
new = prefix + start + body + end + suffix
assert new != original
source.write_text(new)
print("MINIC_PERF_PARSER_TOPLEVEL_LEXER_LOOKAHEAD_V1=APPLIED")
