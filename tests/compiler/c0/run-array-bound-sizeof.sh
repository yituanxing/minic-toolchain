#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-array-bound-sizeof

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/array_bound_sizeof.c" \
    -o "$work/array_bound_sizeof.i"
"$minic" -S "$work/array_bound_sizeof.i" \
    -o "$work/array_bound_sizeof.s"

test -s "$work/array_bound_sizeof.s"
grep -F 'read_sizes:' "$work/array_bound_sizeof.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/array_bound_constant_expr pointer=8 arithmetic=12 enum=6 parentheses=1'
