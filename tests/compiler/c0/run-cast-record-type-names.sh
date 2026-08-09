#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-cast-record-type-names

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/cast_record_type_names.c" \
    -o "$work/cast_record_type_names.i"
"$minic" -S "$work/cast_record_type_names.i" \
    -o "$work/cast_record_type_names.s"

grep -F 'read_holder:' "$work/cast_record_type_names.s" >/dev/null
grep -F 'lw a0, 0(a0)' "$work/cast_record_type_names.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/cast_record_type_names union-cast=1 volatile-cast=1'
