#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-fixed-record-array-zero

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/static_fixed_record_array_zero.c" \
    -o "$work/static_fixed_record_array_zero.i"
"$minic" -S "$work/static_fixed_record_array_zero.i" \
    -o "$work/static_fixed_record_array_zero.s"

grep -F '.type zero_records, @object' "$work/static_fixed_record_array_zero.s" >/dev/null
grep -F '.size zero_records, 48' "$work/static_fixed_record_array_zero.s" >/dev/null
grep -F '.type page_records, @object' "$work/static_fixed_record_array_zero.s" >/dev/null
grep -Fx '.section .bss..page_aligned' "$work/static_fixed_record_array_zero.s" >/dev/null
grep -F '.size page_records, 32' "$work/static_fixed_record_array_zero.s" >/dev/null
grep -F '.type initialized_records, @object' "$work/static_fixed_record_array_zero.s" >/dev/null
grep -F '.size initialized_records, 32' "$work/static_fixed_record_array_zero.s" >/dev/null
grep -F '.section .rodata' "$work/static_fixed_record_array_zero.s" >/dev/null
grep -F 'read_fixed_record_arrays:' "$work/static_fixed_record_array_zero.s" >/dev/null

printf '%s\n' 'PASS compiler/c0/static_fixed_record_array_zero fixed-record=generic zero-init=1 suffix-section=1 suffix-align=1 initialized=shared'
