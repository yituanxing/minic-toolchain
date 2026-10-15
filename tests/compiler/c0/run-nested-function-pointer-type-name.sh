#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-nested-function-pointer-type-name
source="$root/tests/compiler/c0/nested_function_pointer_type_name.c"

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -x c "$source" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
test -s "$work/output.s"
grep -F 'nested_function_pointer_type_name:' "$work/output.s" >/dev/null
grep -F 'jalr ra, t0, 0' "$work/output.s" >/dev/null

if command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then
    riscv64-linux-gnu-gcc -c "$work/output.s" -o "$work/output.o"
fi

printf '%s\n' 'PASS compiler/c0/nested-function-pointer-type-name anonymous=1 nested=1 indirect-call=1'
