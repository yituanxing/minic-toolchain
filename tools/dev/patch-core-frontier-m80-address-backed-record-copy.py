#!/usr/bin/env python3
"""Advance the hot Core frontier after the persisted M80 product."""

from pathlib import Path
from runpy import run_path

M80_MARKER = "M80_ADDRESS_BACKED_RECORD_COPY"
M81_DRIVER = Path("tools/dev/patch-core-frontier-m81-function-address.py")


def main() -> int:
    required = (
        Path("src/core/core_ir.h"),
        Path("src/core/core_ir.c"),
        Path("src/core/core_lower.c"),
        Path("src/target/riscv64/core_codegen.c"),
    )
    if any(M80_MARKER not in path.read_text() for path in required):
        raise SystemExit("M80 persisted product is incomplete")
    namespace = run_path(str(M81_DRIVER))
    m81_main = namespace.get("main")
    if not callable(m81_main):
        raise SystemExit("M81 frontier driver does not expose main()")
    return int(m81_main())


if __name__ == "__main__":
    raise SystemExit(main())
