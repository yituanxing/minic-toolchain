#!/usr/bin/env bash
# A numeric prefix before an ELF relocation symbol is the same affine
# MCExpr as a symbol-first addend. Compare actual emitted bytes and RELA.
set -Eeuo pipefail
: "${MINIAS:?MiniAS executable required}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat >"$work/prefix.s" <<'SRC'
.data
.globl target
.long 0x9b000000 + target
.long target + 0x9b000000
.long (0x800 + target)
.quad 0x12345678 + target
.quad target + 0x12345678
SRC
"$MINIAS" --target=x86_64 -o "$work/mini.o" "$work/prefix.s"
as --64 -o "$work/gnu.o" "$work/prefix.s"
objcopy -j .data -O binary "$work/mini.o" "$work/mini.bin"
objcopy -j .data -O binary "$work/gnu.o" "$work/gnu.bin"
cmp "$work/mini.bin" "$work/gnu.bin"
readelf -rW "$work/mini.o" | awk '$3 ~ /^R_/ {print $1,$3,$5,$6}' >"$work/mini.reloc"
readelf -rW "$work/gnu.o" | awk '$3 ~ /^R_/ {print $1,$3,$5,$6}' >"$work/gnu.reloc"
diff -u "$work/gnu.reloc" "$work/mini.reloc"

# The first operand is permitted only if independently absolute. Two
# relocatable symbols cannot be folded into a fabricated constant addend.
printf '.data\n.long first + second\n' >"$work/invalid.s"
if "$MINIAS" --target=x86_64 -o "$work/invalid.o" "$work/invalid.s" \
    2>"$work/invalid.err"; then
  echo 'MiniAS incorrectly accepted two-symbol sum' >&2
  exit 1
fi
test ! -e "$work/invalid.o"
echo 'MINIAS_MCEXPR_ABSOLUTE_PREFIX=PASS relocations=5 GNU_bytes=exact GNU_reloc=exact invalid_two_symbols=rejected'
