#!/usr/bin/env bash
# Final Section-relative alignment is not a subsection-local calculation.
# Test the entire Source -> MCLayout -> Symbol -> Emission -> DWARF pipeline
# against GNU GAS, including alignment changes propagated across numerically
# ordered subsections parsed in a different source order.
set -Eeuo pipefail
: "$MINIAS"
for tool in as objcopy readelf cmp awk; do command -v "$tool" >/dev/null; done
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat >"$work/base.s" <<'ASM'
.text
.subsection 0
.globl section_start
section_start:
  .byte 0x41,0x42
.subsection 1
.globl sub_one_begin
sub_one_begin:
  .p2align 3,0xcc
.globl sub_one_aligned
sub_one_aligned:
  .byte 0x43
.subsection 0
  .byte 0x44
ASM

cat >"$work/cascade.s" <<'ASM'
.text
.subsection 0
.globl chain_start
chain_start:
  .byte 0x41,0x42
.subsection 2
.p2align 4,0xbb
.globl chain_two
chain_two:
  .byte 0x55
.subsection 1
.p2align 3,0xcc
.globl chain_one
chain_one:
  .byte 0x44
.subsection 0
  .byte 0x43
.size chain_start,.-chain_start
ASM

cat >"$work/interleave.s" <<'ASM'
.text
.subsection 0
  .byte 0x11,0x22,0x33
.subsection 1
  .byte 0x44
.data
  .byte 0x55
.text
.subsection 1
  .p2align 3,0xcc
  .byte 0x66
.subsection 0
  .byte 0x77
ASM

cat >"$work/maxskip.s" <<'ASM'
.text
.subsection 0
  .space 7,0xaa
.subsection 1
maxskip_before:
  .p2align 3,,2
maxskip_after:
  .byte 0x42
ASM

cat >"$work/org.s" <<'ASM'
.text
.subsection 0
  .byte 0x41,0x42
.subsection 1
org_before:
  .org 8,0xcc
org_after:
  .byte 0x43
.subsection 0
  .byte 0x44
ASM

cat >"$work/branch.s" <<'ASM'
.text
.subsection 0
.globl branch_start
branch_start:
  jmp branch_target
  .space 200,0x90
.subsection 2
  .p2align 4,0xcc
.globl branch_target
branch_target:
  nop
.subsection 1
  .p2align 3,0x90
  .byte 0xab
ASM

cat >"$work/dwarf.s" <<'ASM'
.file 1 "subsection.c"
.text
.subsection 0
entry:
.loc 1 1 0
  nop
  nop
.subsection 1
  .p2align 3,0x90
.globl dwarf_aligned
dwarf_aligned:
.loc 1 2 0
  nop
.subsection 0
  nop
ASM

for case in base cascade interleave maxskip org branch dwarf; do
    as --64 -o "$work/$case.gnu.o" "$work/$case.s"
    "$MINIAS" --target=x86_64 -o "$work/$case.mini.o" "$work/$case.s"
    objcopy -O binary -j .text "$work/$case.gnu.o" "$work/$case.gnu.text"
    objcopy -O binary -j .text "$work/$case.mini.o" "$work/$case.mini.text"
    if ! cmp -s "$work/$case.gnu.text" "$work/$case.mini.text"; then
        echo "cross-subsection-text-mismatch:$case" >&2
        exit 1
    fi
    # Only source-defined externally visible symbols are included; private
    # GNU/MiniAS synthetic labels may legitimately have different names.
    readelf -Ws "$work/$case.gnu.o" |
        awk '$5 == "GLOBAL" && $7 != "UND" {print $2, $NF}' |
        sort >"$work/$case.gnu.symbols"
    readelf -Ws "$work/$case.mini.o" |
        awk '$5 == "GLOBAL" && $7 != "UND" {print $2, $NF}' |
        sort >"$work/$case.mini.symbols"
    if ! cmp -s "$work/$case.gnu.symbols" "$work/$case.mini.symbols"; then
        echo "cross-subsection-symbol-mismatch:$case" >&2
        exit 1
    fi
done

# DWARF must reflect the *final* alignment and Section end, not merely
# contain structurally valid .debug_line opcodes.
for variant in gnu mini; do
    readelf --debug-dump=decodedline "$work/dwarf.$variant.o" |
        awk '$1 == "subsection.c" && ($2 == "1" || $2 == "2" || $2 == "-") {print $2, $3}' \
        >"$work/dwarf.$variant.rows"
done
cmp "$work/dwarf.gnu.rows" "$work/dwarf.mini.rows"
grep -q '2 0x8' "$work/dwarf.gnu.rows"
echo "MINIAS_MC_CROSS_SUBSECTION_ALIGNMENT=PASS cases=7 GNU_text=exact GNU_symbols=exact DWARF_PC=exact"
