#!/usr/bin/env python3
"""Single-factor benchmark-only: reuse the existing enum global name hash
for duplicate binding checks in the true file/global scope.

This does NOT create another name hash. Existing bind semantics:
  scope_count == 0 -> search every previously declared enum constant;
  scope_count > 0  -> search only the current nested scope's suffix.
The already-certified minic_parser_find_enum_constant() handles the first
case using earliest-valid global_count and a fail-closed linear fallback.
Parser diagnostics/first-match semantics and all local-scope paths stay intact.
Apply only in disposable CI checkout after building exact original binary.
"""
from pathlib import Path

p = Path("src/frontend/parser_enum.c")
before = p.read_text()
old = """    if (parser == NULL || enumerator_id == MINIC_ENUMERATOR_INVALID ||
        find_enum_constant_in_current_scope(parser, name_span) != MINIC_ENUMERATOR_INVALID ||
        (parser->scope_count != 0U &&
         minic_parser_name_bound_in_current_scope(parser, name_span))) {
"""
new = """    if (parser == NULL || enumerator_id == MINIC_ENUMERATOR_INVALID ||
        (parser->scope_count == 0U
             ? minic_parser_find_enum_constant(parser, name_span)
             : find_enum_constant_in_current_scope(parser, name_span)) != MINIC_ENUMERATOR_INVALID ||
        (parser->scope_count != 0U &&
         minic_parser_name_bound_in_current_scope(parser, name_span))) {
"""
assert before.count(old) == 1, "enum-bind anchor drift / possible already patched"
assert before.count("static bool minic_parser_enum_global_index_sync(") == 1
assert before.count("static MinicEnumeratorId minic_parser_enum_global_linear_fallback(") == 1
assert before.count("bool minic_parser_bind_enum_constant(") == 1
p.write_text(before.replace(old, new, 1))
print("MINIC_PERF_PARSER_ENUM_BIND_EXISTING_HASH_V1=APPLIED")
