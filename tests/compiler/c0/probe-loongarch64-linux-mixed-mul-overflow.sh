#!/bin/sh
# The frozen Linux drm_mode_vrefresh and fbcon_set_font emit checked mixed
# signed/unsigned 32-bit multiplication with differing result signedness.
set -eu
: "${MINIC_CC:?point MINIC_CC at the built compiler}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
src=tests/compiler/c0/loongarch64_linux_mixed_mul_overflow.c
host=${HOST_CC:-cc}
"$host" -E -P -nostdinc "$src" -o "$work/mixed.i"
"$MINIC_CC" --target=loongarch64-linux -fPIE -S \
    "$work/mixed.i" -o "$work/mixed.s"
test "$(grep -Fc '  mul.d $t3, $t0, $t1' "$work/mixed.s")" -eq 3
test "$(grep -Fc '  srli.d $t4, $t3, 32' "$work/mixed.s")" -eq 2
test "$(grep -Fc '  xor $t4, $t3, $t0' "$work/mixed.s")" -eq 1
grep -Fq '  bstrpick.d $t0, $t0, 31, 0' "$work/mixed.s"
grep -Fq '  bstrpick.d $t1, $t1, 31, 0' "$work/mixed.s"
echo 'MINIC_LOONGARCH64_MIXED_MUL=PASS unsigned_result=2 signed_result=1 64bit_exact=3'

cross=${LOONGARCH64_CC:-loongarch64-linux-gnu-gcc-13}
if command -v "$cross" >/dev/null 2>&1; then
    "$cross" -c -x assembler "$work/mixed.s" -o "$work/mixed.o"
    echo 'MINIC_LOONGARCH64_MIXED_MUL_GNU_AS=PASS'
else
    echo 'MINIC_LOONGARCH64_MIXED_MUL_GNU_AS=SKIP missing Loong GNU assembler'
fi

if command -v "$host" >/dev/null 2>&1; then
    "$host" -DMINI_MIXED_OVERFLOW_HOST_TEST -O2 "$src" -o "$work/reference"
    "$work/reference"
    echo 'MINIC_LOONGARCH64_MIXED_MUL_GNU_ORACLE=PASS combinations=72'
else
    echo 'MINIC_LOONGARCH64_MIXED_MUL_GNU_ORACLE=SKIP missing host C compiler'
fi
