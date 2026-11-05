#!/usr/bin/env bash
# x86 code ALIGN after raw DATA: GNU bytes and owner-only Common query.
set -Eeuo pipefail
: "$MINIAS"
for tool in python3 as objcopy cmp; do command -v "$tool" >/dev/null; done
python3 - <<'PY'
from pathlib import Path
core=Path("assembler/src/common_emission.c").read_text()
header=Path("assembler/src/minias_target_api.h").read_text()
x86=Path("assembler/src/x86_64_target.c").read_text()
if "minias_code_align_preceded_by_adjacent_data(const MiniAsCodeAlignSink *);" not in header:
    raise SystemExit("alignment does not use the restricted Common query")
if "minias_code_align_preceded_by_adjacent_data(sink)" not in x86:
    raise SystemExit("x86 bypasses the restricted Common alignment query")
begin=core.index("bool minias_code_align_preceded_by_adjacent_data(")
end=core.index("static bool minias_section_reserve(",begin)
body=core[begin:end]
if "as->emission->last_emitted_fragment_id" not in body:
    raise SystemExit("x86 alignment uses source order instead of final emission order")
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

# A source switch away from .text does not break the physical predecessor
# after final Section/subsection materialization. ELF64 .code32 keeps the
# AMD64 profile, but an embedded .code16 uses real-mode NOP instructions.
cat >"$work/align_modes.s" <<'SRC'
.section .text.direct,"ax",@progbits
.byte 0x41
.p2align 4
ret
.section .text.switch,"ax",@progbits
.byte 0x41
.data
.byte 0x7f
.section .text.switch,"ax",@progbits
.p2align 4
ret
.section .text.switchsub,"ax",@progbits
.byte 0x41
.subsection 1
.byte 0x42
.subsection 0
.p2align 4
ret
.section .text.code32,"ax",@progbits
.code32
.byte 0x41
.p2align 4
ret
.code64
.section .text.code16,"ax",@progbits
.code16
.byte 0x41
.p2align 4
ret
.code64
.section .text.code16gcc,"ax",@progbits
.code16gcc
.byte 0x41
.p2align 4
ret
.code64
SRC
as --64 -o "$work/modes.gnu.o" "$work/align_modes.s"
"$MINIAS" --target=x86_64 -o "$work/modes.mini.o" "$work/align_modes.s"
for section in direct switch switchsub code32 code16 code16gcc; do
    for owner in gnu mini; do
        objcopy -j ".text.$section" -O binary \
            "$work/modes.$owner.o" "$work/$section.$owner.bin"
    done
    cmp "$work/$section.gnu.bin" "$work/$section.mini.bin"
done
echo 'MINIAS_X86_ADJACENT_ALIGN=PASS GNU_text=exact emission_predecessor=1 code16_elf64=1 profiles=6'
