#!/bin/sh
# Original RV64 GCC/glibc TS-18661 declarations + target DataLayout oracle.
# Only header declaration/type identity/sizeof are admitted here; this is
# intentionally NOT a compiler or ABI certification for extended FP values.
set -eu
: "${MINIC_CC:?set MINIC_CC}"
gcc=${RISCV_CC:-riscv64-linux-gnu-gcc}
qemu=${QEMU_RISCV64:-qemu-riscv64}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cat >"$work/consumer.c" <<'SRC'
#include <stdlib.h>
typedef _Float32 rv_f32;
typedef _Float64 rv_f64;
typedef _Float32x rv_f32x;
typedef _Float64x rv_f64x;
typedef _Float128 rv_f128;

/* A real glibc function declaration remains distinct from ordinary float. */
extern _Float32 strtof32(const char *, char **);
int rv_extended_declarations_and_layout(void) {
    rv_f32 *f32_pointer = (rv_f32 *)0;
    rv_f64 *f64_pointer = (rv_f64 *)0;
    rv_f32x *f32x_pointer = (rv_f32x *)0;
    rv_f64x *f64x_pointer = (rv_f64x *)0;
    rv_f128 *f128_pointer = (rv_f128 *)0;
    return sizeof(rv_f32) == 4U && _Alignof(rv_f32) == 4U &&
           sizeof(rv_f64) == 8U && _Alignof(rv_f64) == 8U &&
           sizeof(rv_f32x) == 8U && _Alignof(rv_f32x) == 8U &&
           sizeof(rv_f64x) == 16U && _Alignof(rv_f64x) == 16U &&
           sizeof(rv_f128) == 16U && _Alignof(rv_f128) == 16U &&
           sizeof(f32_pointer) == 8U && sizeof(f64_pointer) == 8U &&
           sizeof(f32x_pointer) == 8U && sizeof(f64x_pointer) == 8U &&
           sizeof(f128_pointer) == 8U ? 42 : 1;
}
SRC
cat >"$work/main.c" <<'SRC'
extern int rv_extended_declarations_and_layout(void);
int main(void) { return rv_extended_declarations_and_layout(); }
SRC
"$gcc" -O0 -std=gnu11 -E -P "$work/consumer.c" -o "$work/consumer.i"
"$gcc" -O0 -c "$work/main.c" -o "$work/main.o"
"$gcc" -O0 -c -x c "$work/consumer.i" -o "$work/reference.o"
"$gcc" -static "$work/main.o" "$work/reference.o" -o "$work/reference"
if ! "$MINIC_CC" --target=riscv64-linux -S "$work/consumer.i" \
     -o "$work/mini.s" >"$work/minic.out" 2>"$work/minic.err"; then
    echo 'RV64_EXTENDED_FLOAT_HEADERS=FAIL stage=minic_glibc_parse' >&2
    sed -n '1,24p' "$work/minic.err" >&2
    exit 1
fi
"$gcc" -c -x assembler "$work/mini.s" -o "$work/mini.o"
"$gcc" -static "$work/main.o" "$work/mini.o" -o "$work/mini"
set +e
timeout 30s "$qemu" "$work/reference" >"$work/reference.out" 2>"$work/reference.err"
gnu_rc=$?
timeout 30s "$qemu" "$work/mini" >"$work/mini.out" 2>"$work/mini.err"
mini_rc=$?
set -e
if [ "$gnu_rc" -ne 42 ] || [ "$mini_rc" -ne 42 ] ||
   ! cmp -s "$work/reference.out" "$work/mini.out" ||
   ! cmp -s "$work/reference.err" "$work/mini.err"; then
    echo "RV64_EXTENDED_FLOAT_HEADERS=FAIL stage=gnu_qemu_differential gcc=$gnu_rc minic=$mini_rc" >&2
    exit 1
fi
echo 'RV64_EXTENDED_FLOAT_HEADERS=PASS glibc_original=1 distinct_ts18661_types=5 sizeof_alignof_gnu_qemu=1 ABI_operations_not_claimed=1'
