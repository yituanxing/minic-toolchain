#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-record-multidimensional-arrays

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/record_multidimensional_arrays.c" \
    -o "$work/record_multidimensional_arrays.i"
"$minic" -S "$work/record_multidimensional_arrays.i" \
    -o "$work/record_multidimensional_arrays.s"

test -s "$work/record_multidimensional_arrays.s"
grep -F 'read_grid:' "$work/record_multidimensional_arrays.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/record_multidimensional_arrays dims=2,3 member-index=2'
