#!/usr/bin/env python3
from pathlib import Path


PATH = Path("src/frontend/ast.c")
OLD = """        if (existing->name_length == name_length &&
            memcmp(existing->name, name, name_length) == 0) {
            return false;
        }
"""
NEW = """        if (name_length == existing->name_length &&
            memcmp(existing->name, name, name_length) == 0) {
            return false;
        }
"""


def rewrite_in_function(text: str, function_name: str, next_function_name: str) -> str:
    start = text.find(f"bool {function_name}(")
    end = text.find(f"\nbool {next_function_name}(", start)
    if start < 0 or end < 0:
        raise SystemExit(f"cannot locate function scope: {function_name}")
    body = text[start:end]
    count = body.count(OLD)
    if count != 1:
        raise SystemExit(f"{function_name}: expected one duplicate-name anchor, found {count}")
    return text[:start] + body.replace(OLD, NEW, 1) + text[end:]


text = PATH.read_text()
# pr76-anonymous-record-members.py intentionally changes duplicate-name handling only for
# record fields. Lua staging has made the same textual condition appear in several symbol
# tables; rewrite the two unrelated occurrences to an equivalent spelling so the Linux patch
# remains scoped to minic_c0_record_add_field instead of matching by accident.
text = rewrite_in_function(text, "minic_c0_program_add_record", "minic_c0_program_add_anonymous_record")
text = rewrite_in_function(text, "minic_c0_program_add_type_alias", "minic_c0_program_expression")
PATH.write_text(text)
print("stabilized Linux anonymous-member patch anchors without semantic changes")
