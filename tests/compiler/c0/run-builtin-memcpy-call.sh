#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0/builtin-memcpy-call

mkdir -p "$work"
"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/builtin_memcpy_call.c" \
    -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"

test "$(grep -c -F '  call memcpy' "$work/output.s")" -eq 2
if grep -F '__builtin_memcpy' "$work/output.s" >/dev/null; then
    printf '%s\n' 'FAIL compiler/c0/builtin_memcpy_call: builtin spelling leaked to assembly' >&2
    exit 1
fi
printf '%s\n' 'PASS compiler/c0/builtin_memcpy_call canonical CALL memcpy'
