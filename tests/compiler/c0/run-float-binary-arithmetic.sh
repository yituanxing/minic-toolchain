#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-float-binary-arithmetic
source="$root/tests/compiler/c0/float_binary_arithmetic.c"

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -x c "$source" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
test -s "$work/output.s"
grep -F 'fadd.s' "$work/output.s" >/dev/null
grep -F 'fsub.s' "$work/output.s" >/dev/null
grep -F 'fmul.s' "$work/output.s" >/dev/null
grep -F 'fdiv.s' "$work/output.s" >/dev/null

if command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then
    riscv64-linux-gnu-gcc -c "$work/output.s" -o "$work/output.o"
fi
printf '%s\n' 'PASS compiler/c0/float-binary-arithmetic add=1 sub=1 mul=1 div=1 binary32=1'
