#!/usr/bin/env bash
# AAELF64 static data relocations include ABS16/PREL16 as well as 32/64.
# There is no generic AArch64 ABS8 relocation; one-byte externals must
# continue to fail closed instead of silently truncating a symbol address.
set -Eeuo pipefail
: "${MINIAS:?set MINIAS to exact-head minic-as}"

work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

cat >"$work/valid.s" <<'ASM'
.section .data
.globl aarch64_narrow_relocations
aarch64_narrow_relocations:
  .hword external_abs16
  .hword external_prel16 - .
  .hword external_prel16 - . + 2
  .word external_abs32
  .xword external_abs64
  .word external_prel32 - .
  .xword external_prel64 - .
ASM
"$MINIAS" --target=aarch64 -o "$work/valid.o" "$work/valid.s"
readelf -h "$work/valid.o" >"$work/header"
readelf -Wr "$work/valid.o" >"$work/relocs"
grep -Eq 'Machine:.*AArch64' "$work/header"
grep -Eq 'Class:[[:space:]]+ELF64' "$work/header"
for name in ABS16 PREL16 ABS32 ABS64 PREL32 PREL64; do
    grep -Eq "R_AARCH64_${name}[[:space:]]" "$work/relocs"
done
test "$(grep -c 'R_AARCH64_PREL16' "$work/relocs")" -eq 2
grep -Eq 'R_AARCH64_PREL16.*external_prel16 \+ 2' "$work/relocs"

cat >"$work/invalid.s" <<'ASM'
.data
  .byte external_abs8
ASM
if "$MINIAS" --target=aarch64 -o "$work/invalid.o" "$work/invalid.s" \
   >"$work/invalid.stdout" 2>"$work/invalid.stderr"; then
    echo 'MINIAS_AARCH64_NARROW=FAIL unsupported_ABS8_accepted' >&2
    exit 1
fi
grep -q 'unsupported-symbol-width:1' "$work/invalid.stderr"
test ! -e "$work/invalid.o"
echo 'MINIAS_AARCH64_NARROW=PASS ABS16=1 PREL16=2 ABS32=1 ABS64=1 PREL32=1 PREL64=1 ABS8=rejected'
