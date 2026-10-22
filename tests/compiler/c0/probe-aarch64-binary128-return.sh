#!/bin/sh
# Independent original-C AAPCS64 binary128 return/call/load/store oracle.
# MiniC owns the producer TU; GNU owns the driver, assembler and linker.
set -eu
: "$MINIC_CC"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
gcc=aarch64-linux-gnu-gcc
qemu=qemu-aarch64
cat >"$work/binary128.c" <<'SRC'
#include <stdlib.h>
long double mini_binary128_strtold(void) {
    long double v = strtold("1.25", (char **)0);
    return v;
}
long double mini_binary128_identity(long double x) {
    return x;
}
long double mini_binary128_literal(void) {
    return 1.25L;
}
long double mini_binary128_stack(
    double a, double b, double c, double d,
    double e, double f, double g, double h, long double x) {
    return x;
}
double mini_binary128_to_double(long double x) {
    return (double)x;
}
SRC
cat >"$work/main.c" <<'SRC'
#include <stdio.h>
extern long double mini_binary128_strtold(void);
extern long double mini_binary128_identity(long double);
extern long double mini_binary128_literal(void);
extern long double mini_binary128_stack(
    double, double, double, double, double, double, double, double, long double);
extern double mini_binary128_to_double(long double);
int main(void) {
    long double x = mini_binary128_strtold();
    long double y = mini_binary128_identity(1.25L);
    long double v = mini_binary128_literal();
    long double z = mini_binary128_stack(1,2,3,4,5,6,7,8,1.25L);
    double d = mini_binary128_to_double(1.25L);
    printf("%La %La %La %La %.2f\n", x, y, v, z, d);
    return x == 1.25L && y == 1.25L && v == 1.25L && z == 1.25L &&
           d == 1.25 ? 0 : 3;
}
SRC
"$gcc" -O0 -std=gnu11 -E -P "$work/binary128.c" -o "$work/binary128.i"
"$gcc" -O0 -std=gnu11 -c "$work/main.c" -o "$work/main.o"
"$gcc" -O0 -c -x c "$work/binary128.i" -o "$work/binary128.gnu.o"
"$gcc" -static "$work/main.o" "$work/binary128.gnu.o" -o "$work/gnu"
timeout 45s "$qemu" "$work/gnu" >"$work/gnu.out" 2>"$work/gnu.err"
"$MINIC_CC" --target=aarch64-linux -S "$work/binary128.i" -o "$work/mini.s"
"$gcc" -c -x assembler "$work/mini.s" -o "$work/mini.o"
"$gcc" -static "$work/main.o" "$work/mini.o" -o "$work/mini"
timeout 45s "$qemu" "$work/mini" >"$work/mini.out" 2>"$work/mini.err"
cmp "$work/gnu.out" "$work/mini.out"
cmp "$work/gnu.err" "$work/mini.err"
echo "AARCH64_BINARY128_RETURN=PASS GNU_QEMU_byte_exact=1 strtold=1 q0_result=1 16byte_storage=1 q0_arg=1 stack_arg=1 trunctfdf2=1 literal_binary128=1"
