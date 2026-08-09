#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-pointer-subtract-assignments

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/pointer_subtract_assignment.c" \
    -o "$work/pointer_subtract_assignment.i"
"$minic" -S "$work/pointer_subtract_assignment.i" \
    -o "$work/pointer_subtract_assignment.s"

test "$(grep -c -F '  call next_slot' "$work/pointer_subtract_assignment.s")" -eq 3
grep -F '  slli a0, a0, 2' "$work/pointer_subtract_assignment.s" >/dev/null
grep -F '  sub a0, t0, a0' "$work/pointer_subtract_assignment.s" >/dev/null
printf '%s\n' "PASS compiler/c0/pointer_subtract_assignment scale=4 lvalue-evaluated-once=1"
