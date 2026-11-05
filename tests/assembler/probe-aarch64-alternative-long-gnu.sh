#!/usr/bin/env bash
# AArch64 Linux alternatives: GAS can reject a .rept size expression when
# its original subsection contains .long-encoded system-register writes.
# Compare MiniAS against the semantically identical GAS mnemonic spelling;
# never treat an invalid GAS object (-Z) as a successful reference.
set -Eeuo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
mini=${MINIAS:-"$root/build/release/bin/minic-as"}
gas=${AARCH64_AS:-aarch64-linux-gnu-as}
objcopy=${AARCH64_OBJCOPY:-aarch64-linux-gnu-objcopy}
test -x "$mini"
command -v "$gas" >/dev/null
command -v "$objcopy" >/dev/null

work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

cat >"$work/long.s" <<'ASM'
.macro altinstruction_entry orig_offset alt_offset cpucap orig_len alt_len
  .word \orig_offset - .
  .word \alt_offset - .
  .hword (\cpucap)
  .byte \orig_len
  .byte \alt_len
.endm
.macro alternative_if cap
  .set .Lasm_alt_mode, 1
  .pushsection .altinstructions, "a"
  altinstruction_entry 663f, 661f, \cap, 664f-663f, 662f-661f
  .popsection
  .subsection 1
  .align 2
661:
.endm
.macro alternative_else
662:
  .if .Lasm_alt_mode == 0
    .subsection 1
  .else
    .previous
  .endif
663:
.endm
.macro alternative_endif
664:
  .org . - (664b-663b) + (662b-661b)
  .org . - (662b-661b) + (664b-663b)
  .if .Lasm_alt_mode == 0
    .previous
  .endif
.endm
.macro nops num
  .rept \num
    nop
  .endr
.endm
.macro alternative_else_nop_endif
  alternative_else
  nops (662b-661b) / 4
  alternative_endif
.endm

.text
.globl alternative_long_entry
alternative_long_entry:
  alternative_if 5
  tbz x0, 31, 1f
  mov x0, #3672
  add x0, x28, x0
  ldp x1, x2, [x0]
  .long 0xd5182101
  .long 0xd5182122
  b 2f
1:
  mrs x0, sctlr_el1
  bic x0, x0, #(1 << 31)
  msr sctlr_el1, x0
2:
  alternative_else_nop_endif
ASM

# Same AArch64 machine words; the mnemonic spelling is the GAS oracle when
# raw .long data fragments make GAS reject the .rept size expression.
sed \
  -e 's/^  \.long 0xd5182101$/  msr s3_0_c2_c1_0, x1/' \
  -e 's/^  \.long 0xd5182122$/  msr s3_0_c2_c1_1, x2/' \
  "$work/long.s" >"$work/gnu.s"

"$mini" --target=aarch64 -o "$work/mini.o" "$work/long.s"
"$gas" -o "$work/gnu.o" "$work/gnu.s"
for section in .text .altinstructions; do
  "$objcopy" -j "$section" -O binary "$work/mini.o" "$work/mini-$section.bin"
  "$objcopy" -j "$section" -O binary "$work/gnu.o" "$work/gnu-$section.bin"
  cmp "$work/mini-$section.bin" "$work/gnu-$section.bin"
done
cmp \
  <(readelf -rW "$work/mini.o" | awk '$3 ~ /^R_AARCH64_/ {print $1,$3,$5,$6}') \
  <(readelf -rW "$work/gnu.o" | awk '$3 ~ /^R_AARCH64_/ {print $1,$3,$5,$6}')

cat >"$work/backwards.s" <<'ASM'
.text
.macro oversized_vector
  .align 7
1:
  .rept 40
    nop
  .endr
  .org 1b + 128
.endm
oversized_vector
ASM
if "$mini" --target=aarch64 -o "$work/backwards-mini.o" \
     "$work/backwards.s" 2>"$work/backwards-mini.err"; then
  echo 'MiniAS accepted a genuinely backwards vector .org' >&2
  exit 1
fi
grep -Eq 'org-backwards(:|-after-layout:)' "$work/backwards-mini.err"
test ! -e "$work/backwards-mini.o"
if "$gas" -o "$work/backwards-gnu.o" \
     "$work/backwards.s" 2>"$work/backwards-gnu.err"; then
  echo 'GNU GAS accepted a genuinely backwards vector .org' >&2
  exit 1
fi

echo 'MINIAS_AARCH64_ALTERNATIVE_LONG_GNU=PASS sections=2 relocations=equal data_to_msr=byte_equal backwards_org=rejected'
