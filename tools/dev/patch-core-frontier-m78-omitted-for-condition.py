#!/usr/bin/env python3
"""Advance the hot Core frontier after the persisted M78 product."""

from pathlib import Path
from runpy import run_path

M78_MARKER = "M78_OMITTED_FOR_CONDITION"
M79_DRIVER = Path("tools/dev/patch-core-frontier-m79-call-frame-return-address.py")


def main() -> int:
    if M78_MARKER not in Path("src/core/core_lower.c").read_text():
        raise SystemExit("M78 persisted product missing from src/core/core_lower.c")
    namespace = run_path(str(M79_DRIVER))
    m79_main = namespace.get("main")
    if not callable(m79_main):
        raise SystemExit("M79 frontier driver does not expose main()")
    return int(m79_main())


if __name__ == "__main__":
    raise SystemExit(main())
