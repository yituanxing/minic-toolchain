#!/usr/bin/env python3
"""Keep the persisted M75 seam and advance the hot frontier through M76.

M75 is already productized in the current refactor branch.  The CI workflow still
invokes this historical patch slot, so use it as the stable tail of the temporary
frontier driver rather than churning the workflow for every small semantic seam.
"""

from pathlib import Path
from runpy import run_path


M75_MARKERS = {
    Path("src/core/core_lower.c"): "M75_POINTER_COMPOUND_ASSIGNMENT_VALUE",
    Path("src/core/core_ir.h"): "M75_POINTER_COMPOUND_ASSIGNMENT_VALUE",
    Path("src/target/riscv64/core_codegen.c"): "M75_POINTER_COMPOUND_ASSIGNMENT_VALUE",
}
M76_DRIVER = Path("tools/dev/patch-core-frontier-m76-single-label-asm-goto.py")


def require_persisted_m75() -> None:
    for path, marker in M75_MARKERS.items():
        text = path.read_text()
        if marker not in text:
            raise SystemExit(
                f"M75 persisted product missing from {path}; "
                "do not silently reconstruct an older product through the hot frontier driver"
            )


def main() -> int:
    require_persisted_m75()
    namespace = run_path(str(M76_DRIVER))
    m76_main = namespace.get("main")
    if not callable(m76_main):
        raise SystemExit("M76 frontier driver does not expose main()")
    return int(m76_main())


if __name__ == "__main__":
    raise SystemExit(main())
