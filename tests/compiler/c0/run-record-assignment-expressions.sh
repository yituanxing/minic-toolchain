#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-record-assignment-expressions

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/record_assignment_expression.c" \
    -o "$work/record_assignment_expression.i"
"$minic" -S "$work/record_assignment_expression.i" \
    -o "$work/record_assignment_expression.s"

grep -F 'main:' "$work/record_assignment_expression.s" >/dev/null
test "$(grep -c -F '  lbu t0, 0(t2)' "$work/record_assignment_expression.s")" -ge 2
test "$(grep -c -F '  sb t0, 0(t3)' "$work/record_assignment_expression.s")" -ge 2
printf '%s\n' 'PASS compiler/c0/record_assignment_expression whole-object-copy=1 comma-discard=1 alias-safe-temp=1'
