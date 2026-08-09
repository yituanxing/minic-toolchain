#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-compound-assignment-expressions

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/compound_assignment_expression.c" \
    -o "$work/compound_assignment_expression.i"
"$minic" -S "$work/compound_assignment_expression.i" \
    -o "$work/compound_assignment_expression.s"

test "$(grep -c -F '  call next_slot' "$work/compound_assignment_expression.s")" -eq 1
grep -F '  slli a0, a0, 2' "$work/compound_assignment_expression.s" >/dev/null
grep -F '  divu a0, t0, a0' "$work/compound_assignment_expression.s" >/dev/null
grep -F '  div a0, t0, a0' "$work/compound_assignment_expression.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/compound_assignment_expression operators=+=,/= result=value lvalue-evaluation=once pointer-scale=4 divide=signed,unsigned'
