#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-array-bound-constant-expressions

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/array_bound_constant_expression.c" \
    -o "$work/array_bound_constant_expression.i"
"$minic" -S "$work/array_bound_constant_expression.i" \
    -o "$work/array_bound_constant_expression.s"
grep -F 'li a0, 1048576' "$work/array_bound_constant_expression.s" >/dev/null
grep -F 'li a0, 35' "$work/array_bound_constant_expression.s" >/dev/null
grep -F 'li a0, 44' "$work/array_bound_constant_expression.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/array_bound_constant_expression scope=local operators=+,-,*,/,% parentheses=1'

"$host_cc" -E -P -x c "$root/tests/compiler/c0/invalid_runtime_array_bound.c" \
    -o "$work/invalid_runtime_array_bound.i"
if "$minic" -S "$work/invalid_runtime_array_bound.i" \
    -o "$work/invalid_runtime_array_bound.s" \
    >"$work/invalid_runtime_array_bound.stdout" \
    2>"$work/invalid_runtime_array_bound.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/invalid_runtime_array_bound: compilation unexpectedly succeeded' >&2
    exit 1
fi
grep -F 'expected integer constant expression' \
    "$work/invalid_runtime_array_bound.stderr" >/dev/null
printf '%s\n' 'PASS compiler/c0/invalid_runtime_array_bound'
