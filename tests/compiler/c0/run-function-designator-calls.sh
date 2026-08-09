#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-function-designator-call

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/function_designator_call.c" \
    -o "$work/function_designator_call.i"
"$minic" -S "$work/function_designator_call.i" \
    -o "$work/function_designator_call.s"

test "$(grep -c -F '  jalr ra, t0, 0' "$work/function_designator_call.s")" -eq 2
printf '%s\n' 'PASS compiler/c0/function_designator_call pointer-call=1 dereferenced-function-designator=1 RV64=jalr'
