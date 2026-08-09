#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-array-bound-integer-casts

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/array_bound_integer_cast.c" \
    -o "$work/array_bound_integer_cast.i"
"$minic" -S "$work/array_bound_integer_cast.i" \
    -o "$work/array_bound_integer_cast.s"

grep -F '  li a0, 32' "$work/array_bound_integer_cast.s" >/dev/null
grep -F '  li a0, 4' "$work/array_bound_integer_cast.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/array_bound_integer_cast enum-cast=1 truncation=uint8 shared-constant-evaluator=1'
