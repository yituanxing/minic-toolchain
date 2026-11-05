#!/usr/bin/env bash
# ELF32 i386 SHT_REL implicit-addend and 8/16/32-bit absolute/PC relocation
# GNU BFD differential. Does not borrow any Linux-specific source or symbol.
set -Eeuo pipefail
: "${MINILD:?}"
for exe in as ld readelf objcopy python3; do
  command -v "$exe" >/dev/null
done
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-i386-rel.XXXXXX")
trap 'rm -rf -- "$work"' EXIT

cat >"$work/input.s" <<'ASM'
.section .text,"ax",@progbits
.global _start
_start:
  .byte data_byte
  .short data_half
  .long data_word
  .byte near_target - .
  .short near_target - .
  .long near_target - .
  ret
.section .data,"aw",@progbits
.global data_byte
data_byte:
  .byte 0x12
.global data_half
data_half:
  .short 0x1234
.global data_word
data_word:
  .long 0x12345678
.section .near,"ax",@progbits
.global near_target
near_target:
  ret
.section .note.GNU-stack,"",@progbits
ASM
cat >"$work/layout.ld" <<'LDS'
OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(_start)
SECTIONS {
  . = 0;
  .text : { *(.text) }
  .data : { *(.data) }
  .near : { *(.near) }
  /DISCARD/ : { *(.note.GNU-stack) }
}
LDS
as --32 -o "$work/input.o" "$work/input.s"
readelf -Wr "$work/input.o" >"$work/input.relocations"
python3 - "$work/input.relocations" <<'PY'
from pathlib import Path
import sys
data = Path(sys.argv[1]).read_text()
for name in ('R_386_8', 'R_386_16', 'R_386_32',
             'R_386_PC8', 'R_386_PC16', 'R_386_PC32'):
    if name not in data:
        raise SystemExit('MINILD_I386_REL=FAIL source_missing_' + name)
PY
ld -m elf_i386 -T "$work/layout.ld" -o "$work/gnu.elf" "$work/input.o"
"$MINILD" -m elf_i386 -T "$work/layout.ld" -o "$work/mini.elf" "$work/input.o"
for name in text data near; do
  objcopy -O binary --only-section=".$name" "$work/gnu.elf" "$work/$name.gnu.bin"
  objcopy -O binary --only-section=".$name" "$work/mini.elf" "$work/$name.mini.bin"
  cmp "$work/$name.gnu.bin" "$work/$name.mini.bin"
done
readelf -Wh "$work/mini.elf" >"$work/mini.header"
grep -Eq 'Class:[[:space:]]+ELF32' "$work/mini.header"
grep -Eq 'Machine:.*80386' "$work/mini.header"
echo 'MINILD_I386_REL=PASS elf32=true GNU_BFD_bytes=exact abs=8,16,32 pcrel=8,16,32'
