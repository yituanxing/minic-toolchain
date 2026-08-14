#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-zero-length-record-arrays

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_zero_length_record_array.c" \
    -o "$work/gnu_zero_length_record_array.i"
"$minic" -S "$work/gnu_zero_length_record_array.i" \
    -o "$work/gnu_zero_length_record_array.s"

test -s "$work/gnu_zero_length_record_array.s"
grep -F 'zero_array_offset:' "$work/gnu_zero_length_record_array.s" >/dev/null
grep -F 'zero_tail_offset:' "$work/gnu_zero_length_record_array.s" >/dev/null
grep -F 'zero_record_size:' "$work/gnu_zero_length_record_array.s" >/dev/null
test "$(grep -c '  li a0, 8' "$work/gnu_zero_length_record_array.s")" -ge 2
grep -F '  li a0, 16' "$work/gnu_zero_length_record_array.s" >/dev/null

printf '%s\n' 'PASS compiler/c0/gnu_zero_length_record_array element=ulong alignment=8 storage=0 member-offset=8 tail-offset=8 record-size=16'
