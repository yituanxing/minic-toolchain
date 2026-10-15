#!/usr/bin/env python3
from pathlib import Path

p = Path("src/target/riscv64/core_codegen.c")
s = p.read_text()

old = """static bool core_scalar_object_intervals_overlap(const CoreScalarObjectInterval *left,\n                                                 const CoreScalarObjectInterval *right) {\n    return left != NULL && right != NULL && left->reusable && right->reusable &&\n           left->block_index == right->block_index &&\n           !(left->last_position < right->first_position ||\n             right->last_position < left->first_position);\n}"""

new = """static bool core_scalar_object_intervals_overlap(const CoreScalarObjectInterval *left,\n                                                 const CoreScalarObjectInterval *right) {\n    if (left == NULL || right == NULL || !left->reusable || !right->reusable) {\n        return false;\n    }\n    if (left->block_index != right->block_index) {\n        return true;\n    }\n    return !(left->last_position < right->first_position ||\n             right->last_position < left->first_position);\n}"""

old_count = s.count(old)
canonical_count = s.count(new)
if old_count == 0 and canonical_count == 1:
    print("MINIC_RISCV64_CORE_OBJECT_SLOT_REUSE_CFG_HOTFIX_V0=ALREADY_CANONICAL")
    raise SystemExit(0)
if old_count != 1 or canonical_count != 0:
    raise SystemExit(f"core object CFG hotfix not safely merged old={old_count} new={canonical_count}")
s = s.replace(old, new, 1)
p.write_text(s)
print("applied RV64 cross-block object-slot reuse safety hotfix")
