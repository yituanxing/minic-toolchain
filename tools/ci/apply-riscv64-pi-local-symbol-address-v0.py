#!/usr/bin/env python3
from pathlib import Path


path = Path("src/target/riscv64/core_codegen.c")
text = path.read_text()

old_comment = """/* M74_GLOBAL_RECORD_ADDRESS / M155_EXTERN_VOID_SYMBOL_ADDRESS_OWNER:
   RV64 global.addr lowers to `la symbol`; the pointee's storage shape is
   irrelevant until a later memory operation.  A declaration-only void symbol
   therefore needs no new load/store ABI support. */
"""
new_comment = """/* M74_GLOBAL_RECORD_ADDRESS / M155_EXTERN_VOID_SYMBOL_ADDRESS_OWNER:
   RV64 global.addr keeps ordinary global/extern symbols on `la symbol`, while
   assembler-local `.L*` symbols use `lla symbol`.  The latter must remain
   PC-relative so early position-independent code does not capture a high-half
   linked address before relocation.  The pointee's storage shape is irrelevant
   until a later memory operation.  A declaration-only void symbol therefore
   needs no new load/store ABI support. */
"""
if text.count(old_comment) != 1:
    raise SystemExit(
        f"{path}: expected one global-address comment anchor, found {text.count(old_comment)}"
    )
text = text.replace(old_comment, new_comment, 1)

old_emit = (
    'fprintf(file, "  la t0, %s\\n", '
    'function->globals[instruction->value.global_id].name)'
)
new_emit = (
    'fprintf(file, "  %s t0, %s\\n",\n'
    '                    strncmp(function->globals[instruction->value.global_id].name, ".L", 2U) == 0\n'
    '                        ? "lla"\n'
    '                        : "la",\n'
    '                    function->globals[instruction->value.global_id].name)'
)
count = text.count(old_emit)
if count != 1:
    raise SystemExit(f"{path}: expected one global-address emitter anchor, found {count}")
text = text.replace(old_emit, new_emit, 1)

if old_emit in text:
    raise SystemExit(f"{path}: stale unconditional global-address `la` emitter remains")
if 'strncmp(function->globals[instruction->value.global_id].name, ".L", 2U)' not in text:
    raise SystemExit(f"{path}: local-symbol selection was not installed")

path.write_text(text)
print("MINIC_RISCV64_PI_LOCAL_SYMBOL_ADDRESS_V0=APPLIED")
