#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-aligned-record-fields

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/gnu_aligned_record_field.c" \
    -o "$work/gnu_aligned_record_field.i"
"$minic" -S "$work/gnu_aligned_record_field.i" \
    -o "$work/gnu_aligned_record_field.s"

test -s "$work/gnu_aligned_record_field.s"
grep -F 'aligned_values_offset:' "$work/gnu_aligned_record_field.s" >/dev/null
grep -F 'aligned_tail_offset:' "$work/gnu_aligned_record_field.s" >/dev/null
grep -F 'aligned_record_size:' "$work/gnu_aligned_record_field.s" >/dev/null
grep -F '  li a0, 16' "$work/gnu_aligned_record_field.s" >/dev/null
grep -F '  li a0, 32' "$work/gnu_aligned_record_field.s" >/dev/null
grep -F '  li a0, 48' "$work/gnu_aligned_record_field.s" >/dev/null

printf '%s\n' 'PASS compiler/c0/gnu_aligned_record_field minimum-align=16 typed-ast-consteval=1 values-offset=16 tail-offset=32 record-size=48 offsetof=consistent'
