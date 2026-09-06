#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-floating-aggregate
source="$root/tests/compiler/c0/static_floating_aggregate.c"

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -x c "$source" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
test -s "$work/output.s"
grep -F 'aXformType:' "$work/output.s" >/dev/null
grep -F '.word 1473454116' "$work/output.s" >/dev/null
grep -F '.word 1065353216' "$work/output.s" >/dev/null
grep -F '.word 1424045017' "$work/output.s" >/dev/null
grep -F '.word 1114636288' "$work/output.s" >/dev/null
grep -F 'floatingZero:' "$work/output.s" >/dev/null
grep -F 'integerToDouble:' "$work/output.s" >/dev/null
grep -F '.dword 4619567317775286272' "$work/output.s" >/dev/null
grep -F 'unsignedToFloat:' "$work/output.s" >/dev/null
grep -F '.word 1091567616' "$work/output.s" >/dev/null
grep -F 'fcvt.d.s' "$work/output.s" >/dev/null
grep -F 'fsgnjn.d' "$work/output.s" >/dev/null
grep -F 'fcvt.s.d' "$work/output.s" >/dev/null

if command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then
    riscv64-linux-gnu-gcc -c "$work/output.s" -o "$work/output.o"
fi

printf '%s\n' 'PASS compiler/c0/static-floating-aggregate sqlite-shape=1 binary32-rounding=checked'
