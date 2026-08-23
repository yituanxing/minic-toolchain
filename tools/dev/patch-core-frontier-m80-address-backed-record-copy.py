#!/usr/bin/env python3
"""Advance the hot Core frontier after the persisted M80 product."""

from pathlib import Path
from runpy import run_path

M81_DRIVER = Path("tools/dev/patch-core-frontier-m81-function-address.py")
M80_PRODUCTS = {
    Path("src/core/core_ir.h"): "M80_ADDRESS_BACKED_RECORD_COPY",
    Path("src/core/core_ir.c"): "M80_ADDRESS_BACKED_RECORD_COPY",
    Path("src/core/core_lower.c"): "M80_ADDRESS_BACKED_RECORD_COPY",
    # The persisted RV64 implementation predates the shared comment marker;
    # verify the actual product seam instead of requiring a cosmetic comment.
    Path("src/target/riscv64/core_codegen.c"): "core_record_copy_supported",
}


def main() -> int:
    for path, marker in M80_PRODUCTS.items():
        if marker not in path.read_text():
            raise SystemExit(f"M80 persisted product missing from {path}: {marker}")

    namespace = run_path(str(M81_DRIVER))
    m81_main = namespace.get("main")
    original_replace_once = namespace.get("replace_once")
    if not callable(m81_main) or not callable(original_replace_once):
        raise SystemExit("M81 frontier driver does not expose main()/replace_once()")

    # core_lower.c now has two matching ADDRESS_OF dispatch lines. The first is
    # lower_expression(), where function designators belong; the later one is
    # core_inline_asm_symbolic_immediate_name(), which must remain untouched.
    # Keep every other M81 anchor strict and resolve only this known ambiguity.
    def frontier_replace_once(text: str, old: str, new: str, name: str) -> str:
        count = text.count(old)
        if name == "lower-function" and count == 2:
            return text.replace(old, new, 1)
        return original_replace_once(text, old, new, name)

    m81_main.__globals__["replace_once"] = frontier_replace_once
    return int(m81_main())


if __name__ == "__main__":
    raise SystemExit(main())
