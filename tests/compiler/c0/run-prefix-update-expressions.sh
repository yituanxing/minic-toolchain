#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-prefix-update-expression

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/prefix_update_expression.c" \
    -o "$work/prefix_update_expression.i"
"$minic" -S "$work/prefix_update_expression.i" \
    -o "$work/prefix_update_expression.s"

grep -F '  addi t0, t0, 1' "$work/prefix_update_expression.s" >/dev/null
grep -F '  addi t0, t0, -1' "$work/prefix_update_expression.s" >/dev/null
grep -F '  addi t0, t0, 4' "$work/prefix_update_expression.s" >/dev/null
grep -F '  addi t0, t0, -4' "$work/prefix_update_expression.s" >/dev/null
test "$(grep -c -F '  mv a0, t0' "$work/prefix_update_expression.s")" -ge 4
printf '%s\n' 'PASS compiler/c0/prefix_update_expression integer=++/-- pointer=++/-- result=new lvalue-address=once'
