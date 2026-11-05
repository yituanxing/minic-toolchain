#!/usr/bin/env bash
# GNU BFD merge.c uses output+flags+entsize+input-sh_addralign for pools;
# reverse-sorted strings choose suffix owners, then first-seen unique atom
# order determines the emitted bytes. Verify all three properties together.
set -Eeuo pipefail
: "${MINILD:?MINILD must point to minic-ld}"
for tool in as ld readelf python3; do command -v "$tool" >/dev/null; done
work=$(mktemp -d "${TMPDIR:-/tmp}/minild-bfd-merge-order.XXXXXXXX")
trap 'rm -rf -- "$work"' EXIT

cat >"$work/a.s" <<'ASM'
.text
.globl _start
_start:
  mov $60,%eax
  xor %edi,%edi
  syscall
.section .rodata.alpha,"aMS",@progbits,1
.globl same_named_a
same_named_a: .asciz "same-content"
.section .rodata.same,"aMS",@progbits,1
.globl same_name_low_align
same_name_low_align: .asciz "low-versus-high"
.section .rodata.order,"aMS",@progbits,1
.globl first_earliest
first_earliest: .asciz "alpha"
.globl long_later
long_later: .asciz "unique-longer-name"
.globl suffix_later
suffix_later: .asciz "longer-name"
.globl last_latest
last_latest: .asciz "omega"
.section .rodata.pad_yes,"aMS",@progbits,1
.p2align 3
.globl full_aligned_input
full_aligned_input: .asciz "pad"
.p2align 3
.section .rodata.pad_no,"aMS",@progbits,1
.p2align 3
.globl short_unaligned_input
short_unaligned_input: .asciz "raw"
.data
.globl ptr_a
ptr_a: .quad same_named_a
.globl ptr_low
ptr_low: .quad same_name_low_align
.globl ptr_first
ptr_first: .quad first_earliest
.globl ptr_long
ptr_long: .quad long_later
.globl ptr_suffix
ptr_suffix: .quad suffix_later
.globl ptr_last
ptr_last: .quad last_latest
.globl ptr_padded
ptr_padded: .quad full_aligned_input
.globl ptr_unpadded
ptr_unpadded: .quad short_unaligned_input
ASM
cat >"$work/b.s" <<'ASM'
.section .rodata.beta,"aMS",@progbits,1
.globl same_named_b
same_named_b: .asciz "same-content"
.section .rodata.same,"aMS",@progbits,1
.p2align 3
.globl same_name_high_align
same_name_high_align: .asciz "low-versus-high"
.data
.globl ptr_b
ptr_b: .quad same_named_b
.globl ptr_high
ptr_high: .quad same_name_high_align
ASM
cat >"$work/order.lds" <<'LDS'
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)
SECTIONS {
  . = 0x400000;
  .text : { *(.text) }
  . = ALIGN(0x1000);
  .rodata : { *(.rodata.alpha) *(.rodata.beta) *(.rodata.same) }
  . = ALIGN(0x1000);
  .order : { *(.rodata.order) }
  . = ALIGN(0x1000);
  .padded : { *(.rodata.pad_yes) }
  . = ALIGN(0x1000);
  .unpadded : { *(.rodata.pad_no) }
  . = ALIGN(0x1000);
  .data : { *(.data) }
}
LDS
as -o "$work/a.o" "$work/a.s"
as -o "$work/b.o" "$work/b.s"
ld -m elf_x86_64 -static -T "$work/order.lds" \
   -o "$work/gnu.elf" "$work/a.o" "$work/b.o"
"$MINILD" -m elf_x86_64 -static -T "$work/order.lds" \
   -o "$work/mini.elf" "$work/a.o" "$work/b.o"

python3 - "$work/gnu.elf" "$work/mini.elf" <<'PY'
from pathlib import Path
import struct
import sys

def observe(path):
    b = Path(path).read_bytes()
    shoff = struct.unpack_from('<Q', b, 40)[0]
    n, names_i = struct.unpack_from('<HH', b, 60)
    shs = [struct.unpack_from('<IIQQQQIIQQ', b, shoff + 64*i)
           for i in range(n)]
    name_sh = shs[names_i]
    names = b[name_sh[4]:name_sh[4] + name_sh[5]]
    byname = {names[s[0]:names.index(0, s[0])].decode(): s for s in shs}
    payload = {name: (byname[name][3], b[byname[name][4]:
                                      byname[name][4] + byname[name][5]])
               for name in ('.rodata', '.order', '.padded', '.unpadded')}
    # Pointer values prove symbol+relocation coordinate remapping without
    # accepting a copied/shifted reference that happens to match the bytes.
    data = byname['.data']
    ptrs = struct.unpack_from('<10Q', b, data[4])
    return payload, ptrs

gnu, gp = observe(sys.argv[1])
mini, mp = observe(sys.argv[2])
assert gp[0] == gp[8], ('GNU same-align, diff-name not folded', gp)
assert gp[1] != gp[9], ('GNU diff-align, same-name incorrectly folded', gp)
assert gp[4] == gp[3] + len('unique-'), ('GNU suffix owner wrong', gp)
assert gp[2] < gp[3] < gp[5], ('GNU first encounter packing changed', gp)
assert len(gnu['.padded'][1]) == 8, 'GNU padded-aligned input loses output pad'
assert len(gnu['.unpadded'][1]) == 4, 'GNU unpadded input gains output pad'
assert (gnu, gp) == (mini, mp), ('GNU/Mini BFD pool differential',
                               (gnu, gp), (mini, mp))
print('MINILD_MERGE_BFD_ORDER=PASS same_align_cross_name=fold '
      'same_name_diff_align=independent suffix_owner=GNU '
      'first_seen_output_order=GNU trailing_pad=conditional '
      'exact_bytes_and_pointers=true')
PY
