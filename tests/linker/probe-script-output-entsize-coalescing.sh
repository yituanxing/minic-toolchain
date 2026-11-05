#!/usr/bin/env bash
# A linker-script output section can concatenate ordinary PROGBITS inputs
# whose ELF sh_entsize values disagree.  The output's width becomes zero;
# this is not an error and is independent of target architecture or name.
set -Eeuo pipefail

repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
minild=${MINILD:-"$repo/build/release/bin/minic-ld"}
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-script-entsize.XXXXXX")
trap 'rm -rf -- "$work"' EXIT

cat >"$work/input.s" <<'ASM'
.text
.globl _start
_start:
    mov $60,%eax
    xor %edi,%edi
    syscall
.section .rodata.plain,"a",@progbits
.p2align 3
.globl plain_word
plain_word:
    .quad 0x1122334455667788
.section .rodata.records,"a",@progbits
.p2align 3
.globl record_word
record_word:
    .quad 0x8877665544332211
.data
.p2align 3
.globl plain_pointer
plain_pointer:
    .quad plain_word
.globl record_pointer
record_pointer:
    .quad record_word
ASM
as -o "$work/input.o" "$work/input.s"

# Kernel tools can construct non-SHF_MERGE PROGBITS tables with nonzero
# sh_entsize.  GNU as's .section syntax does not expose that combination,
# so make the smallest possible ET_REL section-header-only mutation.
python3 - "$work/input.o" <<'PY'
from pathlib import Path
import struct
import sys

path = Path(sys.argv[1])
raw = bytearray(path.read_bytes())
offset = struct.unpack_from('<Q', raw, 40)[0]
count, names_index = struct.unpack_from('<HH', raw, 60)
sections = [struct.unpack_from('<IIQQQQIIQQ', raw, offset + i * 64)
            for i in range(count)]
name_section = sections[names_index]
names = raw[name_section[4]:name_section[4] + name_section[5]]
matched = 0
for i, sh in enumerate(sections):
    name = names[sh[0]:names.index(0, sh[0])].decode()
    if name == '.rodata.records':
        assert sh[1] == 1 and sh[2] == 2 and sh[9] == 0
        struct.pack_into('<Q', raw, offset + i * 64 + 56, 8)
        matched += 1
assert matched == 1
path.write_bytes(raw)
PY

for order in forward reverse; do
    if [[ $order == forward ]]; then
        selector='*(.rodata.plain) *(.rodata.records)'
    else
        selector='*(.rodata.records) *(.rodata.plain)'
    fi
    cat >"$work/$order.lds" <<LDS
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)
SECTIONS {
  . = 0x400000;
  .text : { *(.text) }
  . = ALIGN(0x1000);
  .rodata : { $selector }
  . = ALIGN(0x1000);
  .data : { *(.data) }
}
LDS
    ld -m elf_x86_64 -static -T "$work/$order.lds" \
        -o "$work/$order.gnu.elf" "$work/input.o"
    "$minild" -m elf_x86_64 -static --script="$work/$order.lds" \
        -o "$work/$order.mini.elf" "$work/input.o"
done

python3 - "$work" <<'PY'
from pathlib import Path
import struct
import sys

root = Path(sys.argv[1])
def extract(path):
    raw = path.read_bytes()
    shoff = struct.unpack_from('<Q', raw, 40)[0]
    shnum, shstridx = struct.unpack_from('<HH', raw, 60)
    sections = [struct.unpack_from('<IIQQQQIIQQ', raw, shoff + i * 64)
                for i in range(shnum)]
    hdr = sections[shstridx]
    names = raw[hdr[4]:hdr[4]+hdr[5]]
    by_name = {names[sh[0]:names.index(0, sh[0])].decode(): sh
               for sh in sections}
    data = by_name['.rodata']
    pointers = by_name['.data']
    return (data[1], data[2], data[9],
            raw[data[4]:data[4]+data[5]],
            struct.unpack_from('<QQ', raw, pointers[4]),
            data[3])

for order in ('forward', 'reverse'):
    reference = extract(root / f'{order}.gnu.elf')
    candidate = extract(root / f'{order}.mini.elf')
    assert reference == candidate, (order, reference, candidate)
    typ, flags, entsize, content, pointers, base = candidate
    assert (typ, flags, entsize) == (1, 2, 0)
    wanted = [0x1122334455667788, 0x8877665544332211]
    if order == 'reverse':
        wanted.reverse()
    assert struct.unpack('<QQ', content) == tuple(wanted)
    assert sorted((p - base for p in pointers)) == [0, 8]
    print(f'MINILD_SCRIPT_OUTPUT_ENTSIZE=PASS order={order} '
          'GNU_header_and_bytes=exact sh_entsize=0 reloc_pointers=correct')
PY
