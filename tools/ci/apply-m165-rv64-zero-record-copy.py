#!/usr/bin/env python3
from pathlib import Path

PATH = Path("src/target/riscv64/core_codegen.c")
text = PATH.read_text()

old = "    return size != 0U && alignment != 0U;\n}\n\nstatic bool core_call_frame_address_supported("
new = "    /* M165_ZERO_RECORD_COPY: GNU empty records are addressable semantic\n       objects. RECORD_COPY has already evaluated both address operands; with\n       zero storage bytes the target action is an intentional no-op. */\n    return alignment != 0U;\n}\n\nstatic bool core_call_frame_address_supported("
count = text.count(old)
if count != 1:
    raise SystemExit(f"M165 zero-record copy: expected 1 capability seam, got {count}")
text = text.replace(old, new, 1)
PATH.write_text(text)
print("M165_ZERO_RECORD_COPY_APPLIED")
