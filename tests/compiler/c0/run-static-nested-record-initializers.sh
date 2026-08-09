#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-nested-record-initializer

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/static_nested_record_initializer.c" \
    -o "$work/static_nested_record_initializer.i"
"$minic" -S "$work/static_nested_record_initializer.i" \
    -o "$work/static_nested_record_initializer.s"

grep -F '.section .rodata' "$work/static_nested_record_initializer.s" >/dev/null
grep -F '.type dummy, @object' "$work/static_nested_record_initializer.s" >/dev/null
grep -F '  .zero 8' "$work/static_nested_record_initializer.s" >/dev/null
grep -F '  .byte 16' "$work/static_nested_record_initializer.s" >/dev/null
grep -F '  .zero 3' "$work/static_nested_record_initializer.s" >/dev/null
grep -F '  .word 7' "$work/static_nested_record_initializer.s" >/dev/null
grep -F '.size dummy, 24' "$work/static_nested_record_initializer.s" >/dev/null
if grep -F '.globl dummy' "$work/static_nested_record_initializer.s" >/dev/null; then
    echo 'nested static record leaked external linkage' >&2
    exit 1
fi
printf '%s\n' 'PASS compiler/c0/static_nested_record_initializer union=2 struct=1 null-pointer=2 bitwise=1 padding=rv64 size=24'
