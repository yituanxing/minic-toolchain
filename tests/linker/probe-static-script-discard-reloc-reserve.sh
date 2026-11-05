#!/usr/bin/env bash
# Source-script ownership must precede target synthetic relocation capacity:
# /DISCARD/ never allocates LoongArch static-PIE R_RELATIVE, GOT or IPLT slots.
set -Eeuo pipefail

repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
minias=${MINIAS:-"$repo/build/release/bin/minic-as"}
minild=${MINILD:-"$repo/build/release/bin/minic-ld"}
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-static-discard-reserve.XXXXXX")
trap 'rm -rf -- "$work"' EXIT

cat >"$work/image.lds" <<'LDS'
OUTPUT_ARCH(loongarch)
ENTRY(_start)
PHDRS { text PT_LOAD FLAGS(7); }
SECTIONS {
  . = 0x9000000000100000;
  .text : { *(.text) } :text
  .iplt : { *(.iplt) }
  .got : ALIGN(16) { *(.got) }
  .igot.plt : { *(.igot.plt) }
  .data : { *(.data) }
  /DISCARD/ : { *(.data.dead) }
  .rela.dyn : ALIGN(8) { *(.rela.dyn) }
}
LDS
cat >"$work/live.s" <<'ASM'
.text
.globl _start
_start:
    jirl $r0,$r1,0
.data
.globl live_pointer
live_pointer:
    .quad _start
    .quad absolute_value
.section .data.dead,"aw",@progbits
.globl discarded_pointer
discarded_pointer:
    .quad _start
    .quad _start
ASM
cat >"$work/dead_only.s" <<'ASM'
.text
.globl _start
_start:
    jirl $r0,$r1,0
.section .data.dead,"aw",@progbits
.globl discarded_pointer
discarded_pointer:
    .quad _start
ASM
cat >"$work/absolute.s" <<'ASM'
.globl absolute_value
.set absolute_value, 0x12345678
ASM
"$minias" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/live.o" "$work/live.s"
"$minias" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/dead_only.o" "$work/dead_only.s"
"$minias" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/absolute.o" "$work/absolute.s"
for mode in live dead_only; do
    inputs=("$work/$mode.o")
    [[ $mode == live ]] && inputs+=("$work/absolute.o")
    "$minild" -m elf64loongarch -static -pie -n --no-dynamic-linker \
        --script="$work/image.lds" -o "$work/$mode.elf" "${inputs[@]}"
done

python3 - "$work/live.elf" "$work/dead_only.elf" <<'PY'
from pathlib import Path
import struct
import sys

def extract(path):
    raw = Path(path).read_bytes()
    assert raw[:6] == b'\x7fELF\x02\x01'
    shoff = struct.unpack_from('<Q', raw, 40)[0]
    shnum, shstridx = struct.unpack_from('<HH', raw, 60)
    sections = [struct.unpack_from('<IIQQQQIIQQ', raw, shoff + i * 64)
                for i in range(shnum)]
    strings = sections[shstridx]
    names = raw[strings[4]:strings[4]+strings[5]]
    byname = {names[sh[0]:names.index(0, sh[0])].decode(): sh
              for sh in sections}
    def read_section(name):
        sh = byname[name]
        return raw[sh[4]:sh[4]+sh[5]]
    return byname, read_section

live, bytes_of = extract(sys.argv[1])
assert '.data.dead' not in live
assert live['.got'][5] == 8
relocs = [struct.unpack_from('<QQQ', bytes_of('.rela.dyn'), off)
          for off in range(0, live['.rela.dyn'][5], 24)]
assert relocs == [(live['.data'][3], 3, live['.text'][3]), (0, 0, 0)], relocs
assert struct.unpack_from('<2Q', bytes_of('.data')) == (
    live['.text'][3], 0x12345678)

dead, unused = extract(sys.argv[2])
assert '.data.dead' not in dead
assert '.got' not in dead or dead['.got'][5] == 0
assert '.rela.dyn' not in dead or dead['.rela.dyn'][5] == 0
print('MINILD_STATIC_SCRIPT_DISCARD_RESERVE=PASS mixed_relative=1 none=1 '
      'discarded_relative=0 dead_only_synthetic_got=0 dead_only_rela_dyn=0')
PY
