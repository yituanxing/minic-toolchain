#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-anonymous-record-field-types

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/anonymous_record_field_type.c" \
    -o "$work/anonymous_record_field_type.i"
"$minic" -S "$work/anonymous_record_field_type.i" \
    -o "$work/anonymous_record_field_type.s"

grep -F '  addi a0, a0, 2' "$work/anonymous_record_field_type.s" >/dev/null
grep -F '  lhu a0, 0(a0)' "$work/anonymous_record_field_type.s" >/dev/null
grep -F '  sh t0, 0(t1)' "$work/anonymous_record_field_type.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/anonymous_record_field_type nested-struct-in-union=1 layout=4 delta-offset=2'
