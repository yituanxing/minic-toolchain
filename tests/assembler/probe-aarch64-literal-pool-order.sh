#!/usr/bin/env bash
# GNU AArch64 .ltorg orders mixed literal widths by width, not first use.
# The source intentionally requests the 64-bit literal before the 32-bit one.
set -Eeuo pipefail
: "$MINIAS"
command -v readelf >/dev/null
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat >"$work/literal.s" <<'SRC'
.text
.globl literal_order
literal_order:
  ldr x0, =target64
  ldr w1, =target32
  .ltorg
.section .rodata,"a",@progbits
target32: .long 1
target64: .quad 2
SRC

"$MINIAS" --target=aarch64 -o "$work/mini.o" "$work/literal.s"
readelf -x .text "$work/mini.o" >"$work/mini.bytes"
readelf -Wr "$work/mini.o" >"$work/mini.relocs"
readelf -SW "$work/mini.o" >"$work/mini.sections"

# The two 4-byte LDR instructions precede a 4-byte ABS32 literal at
# offset 8, four padding bytes, then ABS64 at offset 16 (24 total).
grep -Eq '0x00000000[[:space:]]+80000058[[:space:]]+21000018' "$work/mini.bytes"
grep -Eq '^0000000000000008[[:space:]]+.*R_AARCH64_ABS32' "$work/mini.relocs"
grep -Eq '^0000000000000010[[:space:]]+.*R_AARCH64_ABS64' "$work/mini.relocs"
grep -Eq '[[:space:]]\.text[[:space:]]+PROGBITS.*000018[[:space:]]' "$work/mini.sections"

if command -v aarch64-linux-gnu-as >/dev/null &&
   command -v aarch64-linux-gnu-objcopy >/dev/null; then
    aarch64-linux-gnu-as -o "$work/gnu.o" "$work/literal.s"
    aarch64-linux-gnu-objcopy -O binary --only-section=.text \
        "$work/mini.o" "$work/mini.text"
    aarch64-linux-gnu-objcopy -O binary --only-section=.text \
        "$work/gnu.o" "$work/gnu.text"
    cmp "$work/mini.text" "$work/gnu.text"
    readelf -Wr "$work/gnu.o" >"$work/gnu.relocs"
    grep -Eq '^0000000000000008[[:space:]]+.*R_AARCH64_ABS32' "$work/gnu.relocs"
    grep -Eq '^0000000000000010[[:space:]]+.*R_AARCH64_ABS64' "$work/gnu.relocs"
    echo 'MINIAS_A64_LITERAL_POOL=PASS GNU_bytes=equal literal_order=32_before_64'
else
    echo 'MINIAS_A64_LITERAL_POOL=PASS GNU_bytes=SKIP(no-cross-as) literal_order=32_before_64'
fi
