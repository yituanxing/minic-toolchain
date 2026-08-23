#!/usr/bin/env python3
"""Advance the hot Core frontier after the persisted M79 product."""

from pathlib import Path
from runpy import run_path

M79_MARKER = "M79_CALL_FRAME_RETURN_ADDRESS"
M80_DRIVER = Path("tools/dev/patch-core-frontier-m80-address-backed-record-copy.py")


def main() -> int:
    required = (
        Path("src/core/core_ir.h"),
        Path("src/core/core_ir.c"),
        Path("src/core/core_lower.c"),
        Path("src/target/riscv64/core_codegen.c"),
    )
    if any(M79_MARKER not in path.read_text() for path in required):
        raise SystemExit("M79 persisted product is incomplete")
    namespace = run_path(str(M80_DRIVER))
    m80_main = namespace.get("main")
    if not callable(m80_main):
        raise SystemExit("M80 frontier driver does not expose main()")
    return int(m80_main())


if __name__ == "__main__":
    raise SystemExit(main())
