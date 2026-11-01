#!/usr/bin/env bash
# x86 code ALIGN after raw DATA: GNU bytes and owner-only Common query.
set -Eeuo pipefail
: "$MINIAS"
for tool in python3 as objcopy cmp; do command -v "$tool" >/dev/null; done
python3 - <<'PY'
from pathlib import Path
core=Path("assembler/src/assembler.c").read_text()
header=Path("assembler/src/minias_internal.h").read_text()
x86=Path("assembler/src/x86_64_target.c").read_text()
if "minias_code_alignment_preceded_by_adjacent_data(const MiniAs *);" not in header:
    raise SystemExit("alignment still exposes Target Section selector")
if "minias_code_alignment_preceded_by_adjacent_data(as,section)" in x86:
    raise SystemExit("x86 still selects Section for Common alignment query")
begin=core.index("bool minias_code_alignment_preceded_by_adjacent_data(")
end=core.index("static bool minias_section_reserve(",begin)
body=core[begin:end]
for invariant in ("previous->section==owner->section",
                  "previous->subsection==owner->subsection",
                  "previous->offset+previous->size==owner->offset"):
    if invariant not in body:
        raise SystemExit("missing adjacent-source owner invariant: "+invariant)
PY
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/align.s" <<'SRC'
.text
.globl adjacent_source_data
adjacent_source_data:
  .byte 0x61
  .p2align 4
  .byte 0x62
  .p2align 3,,2
  .byte 0x63
  .balign 16,0x90
  .byte 0x64
SRC
as --64 -o "$work/gnu.o" "$work/align.s"
"$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/align.s"
for name in gnu mini; do
    objcopy -j .text -O binary "$work/$name.o" "$work/$name.bin"
done
cmp "$work/gnu.bin" "$work/mini.bin"
echo 'MINIAS_X86_ADJACENT_ALIGN=PASS GNU_text=exact owner_section=1'
