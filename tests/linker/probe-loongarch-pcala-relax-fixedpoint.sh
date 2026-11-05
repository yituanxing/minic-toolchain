#!/usr/bin/env bash
# GNU LoongArch link relaxation must shorten PCALA before final ALIGN.
# The far and .option norelax paths are negative witnesses; max-skip is
# evaluated against final padding, never against assembler reserve length.
set -Eeuo pipefail
: "${MINIAS:?set MINIAS to minic-as}"
: "${MINILD:?set MINILD to minic-ld}"

work=$(mktemp -d "${TMPDIR:-/tmp}/minild-loong-pcala.XXXXXX")
trap 'rm -r -- "$work"' EXIT

cat >"$work/pcala.s" <<'ASM'
.option relax
.section .text.near,"ax",@progbits
.globl _start
_start:
    la.local $a0,near_target
    .p2align 5
.globl near_target
near_target:
    jirl $r0,$r1,0
    .option push
    .option norelax
    la.local $a1,near_target
    .option pop
    la.local $a2,far_target

.section .text.maxskip,"ax",@progbits
.globl maxskip_start
maxskip_start:
    nop
    .p2align 5,,4
.globl maxskip_end
maxskip_end:
    nop

.section .text.far,"ax",@progbits
.globl far_target
far_target:
    jirl $r0,$r1,0
ASM

cat >"$work/image.lds" <<'LDS'
OUTPUT_ARCH(loongarch)
ENTRY(_start)
SECTIONS {
  . = 0x10000;
  .text : { *(.text.near) *(.text.maxskip) }
  . = 0x410000;
  .fartext : { *(.text.far) }
}
LDS

"$MINIAS" --target=loongarch64 -march=loongarch64 -mabi=lp64d \
    -o "$work/input.o" "$work/pcala.s"
"$MINILD" -m elf64loongarch -static -e _start \
    --script="$work/image.lds" -o "$work/mini.elf" "$work/input.o"

gnu_ld=${LOONG_GNU_LD:-}
if [[ -z $gnu_ld ]]; then
    for candidate in loongarch64-linux-gnu-ld loongarch64-unknown-linux-gnu-ld; do
        if command -v "$candidate" >/dev/null 2>&1; then
            gnu_ld=$(command -v "$candidate")
            break
        fi
    done
fi
if [[ -n $gnu_ld ]]; then
    "$gnu_ld" -m elf64loongarch -static -e _start \
        --script="$work/image.lds" -o "$work/gnu.elf" "$work/input.o"
elif [[ ${MINILD_REQUIRE_GNU:-0} == 1 ]]; then
    echo 'MINILD_LOONG_PCALA_GNU_ORACLE=FAIL missing cross linker' >&2
    exit 1
fi

python3 - "$work/mini.elf" "$work/gnu.elf" <<'PY'
from pathlib import Path
import struct
import sys

def elf(path):
    data = Path(path).read_bytes()
    assert data[:6] == b'\x7fELF\x02\x01', path
    shoff = struct.unpack_from('<Q', data, 40)[0]
    count, strings = struct.unpack_from('<HH', data, 60)
    headers = [struct.unpack_from('<IIQQQQIIQQ', data, shoff + 64*i)
               for i in range(count)]
    strhdr = headers[strings]
    names = data[strhdr[4]:strhdr[4]+strhdr[5]]
    sections = {}
    for i, header in enumerate(headers):
        stop = names.find(b'\x00', header[0])
        name = names[header[0]:stop].decode()
        sections[name] = (i, header, data[header[4]:header[4]+header[5]])
    symbols = {}
    if '.symtab' in sections:
        _, symhdr, symbytes = sections['.symtab']
        symstrings = headers[symhdr[6]]
        strdata = data[symstrings[4]:symstrings[4]+symstrings[5]]
        for offset in range(0, len(symbytes), 24):
            name, info, other, shndx, value, size = struct.unpack_from(
                '<IBBHQQ', symbytes, offset)
            end = strdata.find(b'\x00', name)
            symbols[strdata[name:end].decode()] = value
    return sections, symbols

mini_sections, mini_syms = elf(sys.argv[1])
_, text_hdr, text = mini_sections['.text']
assert text_hdr[3] == 0x10000, hex(text_hdr[3])
assert text_hdr[5] == 72, text_hdr[5]
assert mini_syms['near_target'] == 0x10020, hex(mini_syms['near_target'])
assert mini_syms['maxskip_end'] - mini_syms['maxskip_start'] == 4
assert len(mini_sections['.fartext'][2]) == 4

words = lambda b, off: struct.unpack_from('<I', b, off)[0]
assert (words(text, 0) & 0xfe000000) == 0x18000000, hex(words(text, 0))
assert (words(text, 36) & 0xfe000000) == 0x1a000000, 'norelax pair shortened'
assert (words(text, 44) & 0xfe000000) == 0x1a000000, 'far pair shortened'
assert words(text, 40) & 0xffc00000 == 0x02c00000
assert words(text, 48) & 0xffc00000 == 0x02c00000

if Path(sys.argv[2]).exists():
    gnu_sections, gnu_syms = elf(sys.argv[2])
    for name in ('.text', '.fartext'):
        assert mini_sections[name][2] == gnu_sections[name][2], name
    for name in ('_start', 'near_target', 'maxskip_start',
                 'maxskip_end', 'far_target'):
        assert mini_syms[name] == gnu_syms[name], (name,mini_syms[name],gnu_syms[name])
    print('MINILD_LOONG_PCALA_GNU_ORACLE=PASS sections=2 symbols=5')

print('MINILD_LOONG_PCALA_FIXEDPOINT=PASS '
      'PCADDI=1 ALIGN_AFTER_PCALA=1 NORELAX=1 FAR=1 MAXSKIP=1')
PY
