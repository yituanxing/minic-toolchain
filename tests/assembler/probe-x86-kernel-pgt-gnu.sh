#!/usr/bin/env bash
# Reproduce Linux 6.6.143 head_64.S five-level page-table entries with the
# original GNU constant-minus-symbol arithmetic. Only MiniAS replaces GAS.
set -Eeuo pipefail
: "${MINIAS:?Set MINIAS to the exact-head assembler binary}"
for tool in as ld readelf objcopy cmp diff awk; do
    command -v "$tool" >/dev/null
done

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/pgt.s" <<'ASM'
.text
.globl _start
_start: ret
.data
.balign 4096
level4_kernel_pgt:
    .fill 511,8,0
    .quad level3_kernel_pgt - 0xffffffff80000000 + (0x001|0x002|0x004|0x020|0x040)
.balign 4096
level3_kernel_pgt:
    .fill 510,8,0
    .quad level2_kernel_pgt - 0xffffffff80000000 + (0x001|0x002|0|0x020|0|0x040|0|0)
    .quad level2_fixmap_pgt - 0xffffffff80000000 + (0x001|0x002|0x004|0x020|0x040)
.balign 4096
level2_kernel_pgt:
    .quad level2_fixmap_pgt - (0xffffffff80000000 + 0x63)
    .quad level2_fixmap_pgt - 0xffffffff80000000 - 0x20 + 0x63
    .quad level2_fixmap_pgt - 0x10
.balign 4096
level2_fixmap_pgt:
    .quad 0
ASM
cat >"$work/link.ld" <<'LD'
SECTIONS {
    . = 0xffffffff81000000;
    .text : { *(.text) }
    . = 0xffffffff82a00000;
    .data : { *(.data) }
}
LD

as --64 -o "$work/gnu.o" "$work/pgt.s"
"$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/pgt.s"
for flavor in gnu mini; do
    readelf -rW "$work/$flavor.o" |
        awk '/R_X86_64_64/ {print $1, $3, $5, $6, $7}' \
        >"$work/$flavor.reloc"
    ld -m elf_x86_64 -T "$work/link.ld" \
        -o "$work/$flavor.elf" "$work/$flavor.o"
    objcopy -j .data -O binary "$work/$flavor.elf" "$work/$flavor.bin"
done
diff -u "$work/gnu.reloc" "$work/mini.reloc"
cmp "$work/gnu.bin" "$work/mini.bin"
test "$(wc -l <"$work/mini.reloc")" -eq 6
echo 'MINIAS_X86_KERNEL_PGT=PASS GNU_RELA_addends=6/6 GNU_linked_page_tables=exact five_level_present_flags=exact'
