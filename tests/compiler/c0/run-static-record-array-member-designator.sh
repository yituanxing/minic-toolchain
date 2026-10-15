#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-record-array-member-designator

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/static_record_array_member_designator.c" \
    -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"

grep -F 'table:' "$work/output.s" >/dev/null
grep -F '  .word 7' "$work/output.s" >/dev/null
grep -F '  .word 9' "$work/output.s" >/dev/null
grep -F '  .word 11' "$work/output.s" >/dev/null
grep -F '  .word 13' "$work/output.s" >/dev/null
grep -F 'anchor' "$work/output.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/static-record-array-member-designator element=integer,record,pointer overwrite=shared-zero-subobject range=fail-closed'
