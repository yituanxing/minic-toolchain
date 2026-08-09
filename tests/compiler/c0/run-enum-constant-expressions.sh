#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-enum-constant-expressions

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/enum_constant_expression.c" \
    -o "$work/enum_constant_expression.i"
"$minic" -S "$work/enum_constant_expression.i" \
    -o "$work/enum_constant_expression.s"

grep -F '  li a0, 256' "$work/enum_constant_expression.s" >/dev/null
grep -F '  li a0, 264' "$work/enum_constant_expression.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/enum_constant_expression arithmetic=1 prior-enumerator=1 shared-evaluator=1'
