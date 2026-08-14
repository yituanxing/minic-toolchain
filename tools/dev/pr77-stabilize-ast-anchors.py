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


text = PATH.read_text()
record_start = text.find("bool minic_c0_record_add_field(")
record_end = text.find("\nbool ", record_start + 1)
if record_start < 0 or record_end < 0:
    raise SystemExit("cannot locate minic_c0_record_add_field scope")

positions = []
search_from = 0
while True:
    position = text.find(OLD, search_from)
    if position < 0:
        break
    positions.append(position)
    search_from = position + len(OLD)

record_positions = [position for position in positions if record_start <= position < record_end]
if len(record_positions) != 1:
    raise SystemExit(
        f"expected exactly one duplicate-name anchor in minic_c0_record_add_field, found {len(record_positions)}"
    )
if len(positions) < 2:
    raise SystemExit(f"expected duplicate textual anchors outside record fields, found {len(positions)} total")

# pr76-anonymous-record-members.py intentionally changes duplicate-name handling only for
# record fields. Lua staging can create the same textual condition in unrelated symbol tables.
# Rewrite every unrelated occurrence to an equivalent spelling while leaving the one inside
# minic_c0_record_add_field untouched, so the subsequent semantic patch has one structural
# target rather than depending on function names or occurrence counts elsewhere.
record_position = record_positions[0]
parts = []
cursor = 0
for position in positions:
    parts.append(text[cursor:position])
    parts.append(OLD if position == record_position else NEW)
    cursor = position + len(OLD)
parts.append(text[cursor:])
PATH.write_text("".join(parts))
print(
    "stabilized Linux anonymous-member patch anchor structurally without semantic changes"
)
