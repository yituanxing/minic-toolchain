#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-void-pointer-arithmetic
assembly="$work/gnu_void_pointer_arithmetic.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_void_pointer_arithmetic.c" \
    -o "$work/gnu_void_pointer_arithmetic.i"
"$minic" -S "$work/gnu_void_pointer_arithmetic.i" -o "$assembly"

test -s "$assembly"
grep -F 'gnu_void_pointer_add:' "$assembly" >/dev/null
grep -F 'gnu_void_pointer_subtract:' "$assembly" >/dev/null

printf '%s\n' 'PASS compiler/c0/gnu_void_pointer_arithmetic pointee=void stride=1 binary=+,- incomplete-record=unchanged'
