#!/usr/bin/env python3
"""Advance the hot Core frontier after the persisted M76 product."""

from pathlib import Path
from runpy import run_path

M76_MARKERS = {
    Path("src/core/core_lower.c"): "M76_SINGLE_LABEL_ASM_GOTO",
    Path("src/core/core_ir.h"): "M76_SINGLE_LABEL_ASM_GOTO",
    Path("src/target/riscv64/core_codegen.c"): "M76_SINGLE_LABEL_ASM_GOTO",
}
M77_DRIVER = Path("tools/dev/patch-core-frontier-m77-empty-tied-asm-copy.py")


def require_persisted_m76() -> None:
    for path, marker in M76_MARKERS.items():
        if marker not in path.read_text():
            raise SystemExit(f"M76 persisted product missing from {path}")


def main() -> int:
    require_persisted_m76()
    namespace = run_path(str(M77_DRIVER))
    m77_main = namespace.get("main")
    if not callable(m77_main):
        raise SystemExit("M77 frontier driver does not expose main()")
    return int(m77_main())


if __name__ == "__main__":
    raise SystemExit(main())
