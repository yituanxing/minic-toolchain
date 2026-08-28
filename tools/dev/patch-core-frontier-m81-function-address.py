#!/usr/bin/env python3
"""Advance the hot Core frontier after the persisted M81 product."""

from pathlib import Path
from runpy import run_path

M82_DRIVER = Path("tools/dev/patch-core-frontier-m82-binary-pointer-subtract.py")
M81_PRODUCTS = {
    Path("src/core/core_ir.h"): "M81_FUNCTION_ADDRESS_VALUE",
    Path("src/core/core_ir.c"): "M81_FUNCTION_ADDRESS_VALUE",
    Path("src/core/core_lower.c"): "M81_FUNCTION_ADDRESS_VALUE",
    Path("src/target/riscv64/core_codegen.c"): "MINIC_CORE_INSTRUCTION_FUNCTION_ADDRESS",
}


def main() -> int:
    for path, marker in M81_PRODUCTS.items():
        if marker not in path.read_text():
            raise SystemExit(f"M81 persisted product missing from {path}: {marker}")
    namespace = run_path(str(M82_DRIVER))
    m82_main = namespace.get("main")
    if not callable(m82_main):
        raise SystemExit("M82 frontier driver does not expose main()")
    return int(m82_main())


if __name__ == "__main__":
    raise SystemExit(main())
