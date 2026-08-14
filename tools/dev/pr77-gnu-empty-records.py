#!/usr/bin/env python3
from pathlib import Path


def replace_once(path: str, old: str, new: str, label: str) -> None:
    target = Path(path)
    text = target.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one anchor, found {count}")
    target.write_text(text.replace(old, new, 1))


# GCC's GNU C extension permits structures/unions with no members. A complete empty
# record has size 0; completion must therefore not be conflated with field_count > 0.
replace_once(
    "src/frontend/ast.c",
    """    if (record->is_complete || record->field_count == 0U) {
        return false;
    }
""",
    """    if (record->is_complete) {
        return false;
    }
""",
    "empty-record-completion",
)

# RV64 layout previously used storage_size == 0 as the pending-layout sentinel. That is
# invalid once zero-sized GNU records exist. alignment == 0 already means 'not laid out',
# while every completed layout has an alignment of at least 1, so use it as the sentinel.
replace_once(
    "src/target/riscv64/layout.c",
    """        if (record == NULL || !record->is_complete || record->storage_size == 0U ||
            record->alignment == 0U) {
""",
    """        if (record == NULL || !record->is_complete || record->alignment == 0U) {
""",
    "empty-record-type-layout",
)
replace_once(
    "src/target/riscv64/layout.c",
    """        return record != NULL && record->is_complete && record->storage_size == 0U;
""",
    """        return record != NULL && record->is_complete && record->alignment == 0U;
""",
    "empty-record-pending",
)
replace_once(
    "src/target/riscv64/layout.c",
    """    if (record->field_count == 0U) {
        return false;
    }

    storage_size = 0U;
""",
    """    storage_size = 0U;
""",
    "empty-record-layout-body",
)
replace_once(
    "src/target/riscv64/layout.c",
    """            if (!record->is_complete || record->storage_size != 0U) {
                continue;
            }
""",
    """            if (!record->is_complete || record->alignment != 0U) {
                continue;
            }
""",
    "empty-record-layout-progress",
)

print("staged GNU empty records with size=0 and alignment-based layout completion state")
