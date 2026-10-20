#!/bin/sh
# Original C 128-bit long-double by-value return through LP64D a0/a1.
# GNU caller / independent GNU baseline, same preprocessed producer TU.
set -eu
: "$MINIC_CC"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
gcc=loongarch64-linux-gnu-gcc-13
qemu=qemu-loongarch64
cat >"$work/binary128.c" <<'SRC'
#include <stdlib.h>
long double mini_binary128_strtold(void) {
    long double v = strtold("1.25", (char **)0);
    return v;
}
SRC
cat >"$work/main.c" <<'SRC'
#include <stdio.h>
extern long double mini_binary128_strtold(void);
int main(void) {
    long double x = mini_binary128_strtold();
    printf("%La\n", x);
    return x == 1.25L ? 0 : 3;
}
SRC
"$gcc" -O0 -std=gnu11 -E -P "$work/binary128.c" -o "$work/binary128.i"
"$gcc" -O0 -std=gnu11 -c "$work/main.c" -o "$work/main.o"
"$gcc" -O0 -c -x c "$work/binary128.i" -o "$work/binary128.gnu.o"
"$gcc" -static "$work/main.o" "$work/binary128.gnu.o" -o "$work/gnu"
timeout 45s "$qemu" "$work/gnu" >"$work/gnu.out" 2>"$work/gnu.err"
"$MINIC_CC" --target=loongarch64-linux -S "$work/binary128.i" -o "$work/mini.s"
"$gcc" -c -x assembler "$work/mini.s" -o "$work/mini.o"
"$gcc" -static "$work/main.o" "$work/mini.o" -o "$work/mini"
timeout 45s "$qemu" "$work/mini" >"$work/mini.out" 2>"$work/mini.err"
cmp "$work/gnu.out" "$work/mini.out"
cmp "$work/gnu.err" "$work/mini.err"
echo "LOONGARCH64_BINARY128_RETURN=PASS GNU_QEMU_byte_exact=1 strtold=1 a0_a1_result=1 16byte_storage=1"
