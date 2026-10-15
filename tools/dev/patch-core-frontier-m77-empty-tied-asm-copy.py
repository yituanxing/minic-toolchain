#!/usr/bin/env python3
"""Advance the hot Core frontier after the persisted M77 product."""

from pathlib import Path
from runpy import run_path

M77_MARKER = "M77_EMPTY_TIED_ASM_COPY"
M78_DRIVER = Path("tools/dev/patch-core-frontier-m78-omitted-for-condition.py")


def main() -> int:
    if M77_MARKER not in Path("src/core/core_lower.c").read_text():
        raise SystemExit("M77 persisted product missing from src/core/core_lower.c")
    namespace = run_path(str(M78_DRIVER))
    m78_main = namespace.get("main")
    if not callable(m78_main):
        raise SystemExit("M78 frontier driver does not expose main()")
    return int(m78_main())


if __name__ == "__main__":
    raise SystemExit(main())
