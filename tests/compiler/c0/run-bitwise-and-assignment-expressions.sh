#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-bitwise-and-assignment-expressions

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/bitwise_and_assignment_expression.c" \
    -o "$work/bitwise_and_assignment_expression.i"
"$minic" -S "$work/bitwise_and_assignment_expression.i" \
    -o "$work/bitwise_and_assignment_expression.s"

test "$(grep -c -F '  call next_flags' "$work/bitwise_and_assignment_expression.s")" -eq 1
grep -F '  and a0, t0, a0' "$work/bitwise_and_assignment_expression.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/bitwise_and_assignment_expression integer=1 lvalue-evaluated-once=1'
