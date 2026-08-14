#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-record-length-one-arrays

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/record_length_one_array.c" \
    -o "$work/record_length_one_array.i"
"$minic" -S "$work/record_length_one_array.i" \
    -o "$work/record_length_one_array.s"

test -s "$work/record_length_one_array.s"
grep -F 'read_first:' "$work/record_length_one_array.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/record_length_one_array identity=array count=1 subscript=1 scalar-neighbor=1'
