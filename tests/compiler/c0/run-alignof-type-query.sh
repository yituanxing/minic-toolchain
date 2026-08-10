#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-alignof-type-query

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/alignof_type_query.c" \
    -o "$work/alignof_type_query.i"
"$minic" -S "$work/alignof_type_query.i" -o "$work/alignof_type_query.s"
test -s "$work/alignof_type_query.s"
grep -F 'alignof_ulong:' "$work/alignof_type_query.s" >/dev/null
grep -F 'alignof_record:' "$work/alignof_type_query.s" >/dev/null
grep -F '  li a0, 8' "$work/alignof_type_query.s" >/dev/null
grep -F '  li a0, 16' "$work/alignof_type_query.s" >/dev/null

printf '%s\n' 'PASS compiler/c0/alignof_type_query spellings=_Alignof,__alignof,__alignof__ scalar=8 aligned-record=16 service=shared-data-layout static-assert=1'
