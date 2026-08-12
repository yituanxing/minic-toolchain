#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-runtime-record-array-initializers

mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/runtime_record_array_initializer.c" \
    -o "$work/runtime_record_array_initializer.i"
"$minic" -S "$work/runtime_record_array_initializer.i" \
    -o "$work/runtime_record_array_initializer.s"
grep -F 'linux_guid_compound_literal:' "$work/runtime_record_array_initializer.s" >/dev/null
grep -F '  sb ' "$work/runtime_record_array_initializer.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/runtime_record_array_initializer compound-literal=1 fixed-array-field=16 scalar-elements=1'

"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/invalid_record_array_brace_elision.c" \
    -o "$work/invalid_record_array_brace_elision.i"
if "$minic" -S "$work/invalid_record_array_brace_elision.i" \
    -o "$work/invalid_record_array_brace_elision.s" \
    >"$work/invalid.stdout" 2>"$work/invalid.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/invalid_record_array_brace_elision: unexpectedly succeeded' >&2
    exit 1
fi
grep -F 'runtime record array field initializer requires braces' \
    "$work/invalid.stderr" >/dev/null
printf '%s\n' 'PASS compiler/c0/invalid_record_array_brace_elision fail-closed=1'
