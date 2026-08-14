#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-record-typedef-array-fields

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=c11 -x c "$root/tests/compiler/c0/record_typedef_array_field.c" \
    -o "$work/record_typedef_array_field.i"
"$minic" -S "$work/record_typedef_array_field.i" \
    -o "$work/record_typedef_array_field.s"

test -s "$work/record_typedef_array_field.s"
grep -F 'read_reg:' "$work/record_typedef_array_field.s" >/dev/null
grep -F 'context_size:' "$work/record_typedef_array_field.s" >/dev/null
grep -F '  li a0, 40' "$work/record_typedef_array_field.s" >/dev/null

printf '%s\n' 'PASS compiler/c0/record_typedef_array_field typedef-array=4 member=array indexing=1 record-size=40'
