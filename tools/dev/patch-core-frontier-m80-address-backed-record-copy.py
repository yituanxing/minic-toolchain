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
    if not callable(m81_main):
        raise SystemExit("M81 frontier driver does not expose main()")
    return int(m81_main())


if __name__ == "__main__":
    raise SystemExit(main())
