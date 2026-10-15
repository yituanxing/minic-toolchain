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

# Subsequent, already P1-verified defined-before-use reuse deliberately
# replaces the hotfix's unconditional cross-block interference with a
# strictly gated exception.  Recognize this exact final implementation,
# not merely a marker that could conceal a partial/unsafe merge.
verified_crossblock = """static bool core_scalar_object_intervals_overlap(const CoreScalarObjectInterval *left,
                                                 const CoreScalarObjectInterval *right) {
    if (left == NULL || right == NULL || !left->reusable || !right->reusable) {
        return false;
    }
    if (left->block_index != right->block_index) {
        return !(p1_crossblock_current_function_enabled &&
                 left->can_share_across_blocks &&
                 right->can_share_across_blocks);
    }
    return !(left->last_position < right->first_position ||
             right->last_position < left->first_position);
}"""
if old_count == 0 and canonical_count == 0 and s.count(verified_crossblock) == 1:
    prerequisites = (
        "/* P1_DEFINED_BEFORE_USE_CROSSBLOCK_V1:",
        "static bool core_scalar_object_first_access_defines(",
        "static bool p1_crossblock_current_function_enabled = true;",
        "strcmp(focus, symbol_name) == 0;",
    )
    if any(s.count(marker) != 1 for marker in prerequisites):
        raise SystemExit("core object CFG hotfix incomplete later P1 crossblock implementation")
    print("MINIC_RISCV64_CORE_OBJECT_SLOT_REUSE_CFG_HOTFIX_V0=ALREADY_CANONICAL_PLUS_P1_CROSSBLOCK")
    raise SystemExit(0)
if old_count != 1 or canonical_count != 0:
    raise SystemExit(f"core object CFG hotfix not safely merged old={old_count} new={canonical_count}")
s = s.replace(old, new, 1)
p.write_text(s)
print("applied RV64 cross-block object-slot reuse safety hotfix")
