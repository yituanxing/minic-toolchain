#!/bin/sh
# GNU/QEMU AAPCS64 oracle: va_list forwarded across a function boundary.
# The reader must traverse GP and FP register banks with correct cursors.
set -eu
: "${MINIC_CC:?set MINIC_CC to the actual minic-cc}"
w=$(mktemp -d)
trap 'rm -rf "$w"' EXIT HUP INT TERM
cat >"$w/forward.c" <<'SRC'
#include <stdarg.h>
#include <string.h>
static int readargs(va_list a) {
    const char *p = va_arg(a, const char *);
    int n = va_arg(a, int);
    double d = va_arg(a, double);
    return strcmp(p, "/tmp/minic/pressure.lua") == 0 &&
           n == 42 && d == 1.25;
}
static int invoke(const char *fmt, ...) {
    va_list a;
    int result;
    va_start(a, fmt);
    result = readargs(a);
    va_end(a);
    return result;
}
int main(void) {
    return invoke("%s%d%f", "/tmp/minic/pressure.lua", 42, 1.25)
           ? 42 : 1;
}
SRC
gcc=aarch64-linux-gnu-gcc
qemu=qemu-aarch64
"$gcc" -E -P -std=c99 "$w/forward.c" -o "$w/forward.i"
"$gcc" -static -x c "$w/forward.i" -o "$w/gnu"
MINIC_M1_TRACE=1 "$MINIC_CC" --target=aarch64-linux -S \
    "$w/forward.i" -o "$w/mini.s"
"$gcc" -static -x assembler "$w/mini.s" -o "$w/mini"
for kind in gnu mini; do
    set +e
    timeout 45s "$qemu" "$w/$kind" >"$w/$kind.out" 2>"$w/$kind.err"
    result=$?
    set -e
    if [ "$result" -ne 42 ]; then
        echo "AARCH64_VA_FORWARD=FAIL compiler=$kind status=$result" >&2
        sed -n '1,16p' "$w/$kind.err" >&2
        exit 1
    fi
done
cmp "$w/gnu.out" "$w/mini.out"
cmp "$w/gnu.err" "$w/mini.err"
echo "AARCH64_VA_FORWARD=PASS GNU_QEMU_diff=1 GP=1 FP=1 forwarded_list=1"
