#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-builtin-va-list

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/builtin_va_list.c" \
    -o "$work/builtin_va_list.i"
"$minic" -S "$work/builtin_va_list.i" \
    -o "$work/builtin_va_list.s"

test -s "$work/builtin_va_list.s"
grep -F 'builtin_va_list_as_pointer:' "$work/builtin_va_list.s" >/dev/null
grep -F 'builtin_va_list_roundtrip:' "$work/builtin_va_list.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/builtin_va_list target-model=void-pointer typedef=1 roundtrip=1'
