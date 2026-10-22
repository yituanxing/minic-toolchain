#!/bin/sh
# LoongArch LP64D alloca: dynamic SP must not move fixed frame slots.
# GNU caller/callee supplies reference ABI while MiniC compiles the
# allocation function; check both call register/stack and local lifetime.
set -eu
: "$MINIC_CC"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
gcc=loongarch64-linux-gnu-gcc-13
qemu=qemu-loongarch64
cat >"$work/alloca.c" <<'SRC'
extern long oracle_sum10(long,long,long,long,long,long,long,long,long,long);
long test_alloca(unsigned long n) {
    unsigned char *p = (unsigned char *)__builtin_alloca(n);
    unsigned long i;
    long total = 0;
    for (i = 0; i < n; ++i)
        p[i] = (unsigned char)(i * 3UL + 1UL);
    for (i = 0; i < n; ++i)
        total += p[i];
    total += oracle_sum10(1,2,3,4,5,6,7,8,9,10);
    /* Crucial: the outgoing stack argument region must NOT damage alloca. */
    for (i = 0; i < n; ++i)
        total += p[i];
    return total;
}
SRC
cat >"$work/main.c" <<'SRC'
#include <stdio.h>
extern long test_alloca(unsigned long n);
long oracle_sum10(long a,long b,long c,long d,long e,long f,
                  long g,long h,long i,long j) {
    return a+b+c+d+e+f+g+h+i+j;
}
int main(void) {
    unsigned long lengths[] = {1, 7, 16, 19, 100, 257};
    unsigned long i;
    for (i=0; i<sizeof(lengths)/sizeof(lengths[0]); ++i)
        printf("%lu=%ld\n", lengths[i], test_alloca(lengths[i]));
    return 0;
}
SRC
"$gcc" -O0 -std=gnu11 -E -P "$work/alloca.c" -o "$work/alloca.i"
"$gcc" -O0 -c "$work/main.c" -o "$work/main.o"
"$gcc" -O0 -c -x c "$work/alloca.i" -o "$work/alloca.gnu.o"
"$gcc" -static "$work/main.o" "$work/alloca.gnu.o" -o "$work/gnu"
timeout 45s "$qemu" "$work/gnu" >"$work/gnu.out" 2>"$work/gnu.err"
"$MINIC_CC" --target=loongarch64-linux -S "$work/alloca.i" -o "$work/mini.s"
"$gcc" -c -x assembler "$work/mini.s" -o "$work/mini.o"
"$gcc" -static "$work/main.o" "$work/mini.o" -o "$work/mini"
timeout 45s "$qemu" "$work/mini" >"$work/mini.out" 2>"$work/mini.err"
cmp "$work/gnu.out" "$work/mini.out"
cmp "$work/gnu.err" "$work/mini.err"
echo "LOONGARCH64_DYNAMIC_ALLOCA=PASS GNU_QEMU_byte_exact=1 varying_lengths=6 overflow_GP_call=1 preserved_contents=1"
