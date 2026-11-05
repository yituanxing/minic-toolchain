#!/usr/bin/env bash
# GNU keeps SHF_MERGE inputs with different sh_addralign in separate pools,
# even when one linker-script output command collects both classes. Input
# names happen to differ here, but alignment (not name) is the GNU pool key.
set -Eeuo pipefail

repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
minild=${MINILD:-"$repo/build/release/bin/minic-ld"}
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-merge-pool.XXXXXX")
trap 'rm -rf -- "$work"' EXIT

cat >"$work/pools.s" <<'ASM'
.text
.globl _start
_start:
    mov $60,%eax
    xor %edi,%edi
    syscall
.section .rodata.str1.1,"aMS",@progbits,1
.globl string_one
string_one:
    .asciz "repeated-string"
.section .rodata.str1.8,"aMS",@progbits,1
.p2align 3
.globl string_eight
string_eight:
    .asciz "repeated-string"
.data
.globl ptr_one
ptr_one:
    .quad string_one
.globl ptr_eight
ptr_eight:
    .quad string_eight
ASM
cat >"$work/pools.lds" <<'LDS'
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)
SECTIONS {
  . = 0x400000;
  .text : { *(.text) }
  . = ALIGN(4096);
  .rodata : { *(.rodata.str1.1) *(.rodata.str1.8) }
  . = ALIGN(4096);
  .data : { *(.data) }
}
LDS
as -o "$work/pools.o" "$work/pools.s"
ld -m elf_x86_64 -static --script="$work/pools.lds" \
    -o "$work/gnu.elf" "$work/pools.o"
"$minild" -m elf_x86_64 -static --script="$work/pools.lds" \
    -o "$work/mini.elf" "$work/pools.o"

python3 - "$work/gnu.elf" "$work/mini.elf" <<'PY'
from pathlib import Path
import struct
import sys

def read(path):
    b = Path(path).read_bytes()
    off = struct.unpack_from('<Q', b, 40)[0]
    n, ni = struct.unpack_from('<HH', b, 60)
    sections = [struct.unpack_from('<IIQQQQIIQQ', b, off + i * 64)
                for i in range(n)]
    strings = sections[ni]
    names = b[strings[4]:strings[4] + strings[5]]
    byname = {names[s[0]:names.index(0, s[0])].decode(): s
              for s in sections}
    ro, data = byname['.rodata'], byname['.data']
    pointers = struct.unpack_from('<QQ', b, data[4])
    content = b[ro[4]:ro[4] + ro[5]]
    return ro, content, pointers

gnu, gb, gp = read(sys.argv[1])
mini, mb, mp = read(sys.argv[2])
assert gp[0] != gp[1], ('GNU unexpectedly coalesced distinct pools', gp)
assert (gnu[5], gb, gp) == (mini[5], mb, mp), (
    'GNU/Mini pool layout mismatch', (gnu[5], gp), (mini[5], mp))
print('MINILD_MERGE_POOL_IDENTITY=PASS distinct_input_alignment_pools=1 '
      'GNU_elf_bytes_and_pointers=exact')
PY
