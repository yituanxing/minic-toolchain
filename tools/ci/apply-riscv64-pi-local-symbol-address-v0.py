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
   RV64 global.addr must keep assembler-local `.L*` symbols PC-relative in
   early position-independent code.  In the ordinary backend this patch selects
   `lla` only for `.L*` symbols and leaves global/extern symbols on `la`.  If the
   Linux static-PCREL profile has already promoted the whole emitter to `lla`,
   preserve that stronger policy instead of weakening it.  The pointee's storage
   shape is irrelevant until a later memory operation.  A declaration-only void
   symbol therefore needs no new load/store ABI support. */
"""

raw_emit = (
    'fprintf(file, "  la t0, %s\\n", '
    'function->globals[instruction->value.global_id].name)'
)
static_emit = (
    'fprintf(file, "  lla t0, %s\\n", '
    'function->globals[instruction->value.global_id].name)'
)
selector = (
    'strncmp(function->globals[instruction->value.global_id].name, ".L", 2U) == 0'
)
conditional_emit = (
    'fprintf(file, "  %s t0, %s\\n",\n'
    '                    strncmp(function->globals[instruction->value.global_id].name, ".L", 2U) == 0\n'
    '                        ? "lla"\n'
    '                        : "la",\n'
    '                    function->globals[instruction->value.global_id].name)'
)
static_marker = "M196_LINUX_STATIC_PCREL_GLOBAL_ADDRESS"

old_comment_count = text.count(old_comment)
new_comment_count = text.count(new_comment)
if old_comment_count == 1 and new_comment_count == 0:
    text = text.replace(old_comment, new_comment, 1)
elif old_comment_count == 0 and new_comment_count == 1:
    pass
else:
    raise SystemExit(
        f"{path}: expected exactly one old or new global-address comment "
        f"(old={old_comment_count}, new={new_comment_count})"
    )

raw_count = text.count(raw_emit)
static_count = text.count(static_emit)
selector_count = text.count(selector)
has_static_profile = static_marker in text

if selector_count == 1:
    if raw_count != 0 or static_count != 0:
        raise SystemExit(
            f"{path}: ambiguous PI global-address state "
            f"(raw={raw_count}, static={static_count}, selector={selector_count})"
        )
    mode = "conditional-already"
elif selector_count != 0:
    raise SystemExit(f"{path}: expected at most one local-symbol selector, found {selector_count}")
elif has_static_profile:
    if static_count != 1 or raw_count != 0:
        raise SystemExit(
            f"{path}: static-PCREL marker present but emitter state is unexpected "
            f"(raw={raw_count}, static={static_count})"
        )
    mode = "static-pcrel-preserved"
else:
    if raw_count != 1 or static_count != 0:
        raise SystemExit(
            f"{path}: expected one raw global-address emitter "
            f"(raw={raw_count}, static={static_count})"
        )
    text = text.replace(raw_emit, conditional_emit, 1)
    mode = "conditional-installed"

if has_static_profile:
    if text.count(static_emit) != 1:
        raise SystemExit(f"{path}: static-PCREL global-address emitter was not preserved")
else:
    if text.count(selector) != 1:
        raise SystemExit(f"{path}: local-symbol selection was not installed")

path.write_text(text)
print(f"MINIC_RISCV64_PI_LOCAL_SYMBOL_ADDRESS_V0=APPLIED mode={mode}")
