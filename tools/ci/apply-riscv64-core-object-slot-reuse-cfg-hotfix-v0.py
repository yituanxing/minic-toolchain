#!/usr/bin/env python3
from pathlib import Path

p = Path("src/target/riscv64/core_codegen.c")
s = p.read_text()

old = """static bool core_scalar_object_intervals_overlap(const CoreScalarObjectInterval *left,\n                                                 const CoreScalarObjectInterval *right) {\n    return left != NULL && right != NULL && left->reusable && right->reusable &&\n           left->block_index == right->block_index &&\n           !(left->last_position < right->first_position ||\n             right->last_position < left->first_position);\n}"""

new = """static bool core_scalar_object_intervals_overlap(const CoreScalarObjectInterval *left,\n                                                 const CoreScalarObjectInterval *right) {\n    if (left == NULL || right == NULL || !left->reusable || !right->reusable) {\n        return false;\n    }\n    if (left->block_index != right->block_index) {\n        return true;\n    }\n    return !(left->last_position < right->first_position ||\n             right->last_position < left->first_position);\n}"""

if old not in s:
    raise SystemExit("core object slot reuse overlap helper not found")
s = s.replace(old, new, 1)
p.write_text(s)
print("applied RV64 cross-block object-slot reuse safety hotfix")
