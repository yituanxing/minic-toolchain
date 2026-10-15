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
grep -F 'runtime_record_array_local:' "$work/runtime_record_array_initializer.s" >/dev/null
grep -F 'runtime_record_array_designated:' "$work/runtime_record_array_initializer.s" >/dev/null
grep -F 'runtime_inferred_record_array:' "$work/runtime_record_array_initializer.s" >/dev/null
grep -F '  sb ' "$work/runtime_record_array_initializer.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/runtime_record_array_initializer compound-literal=1 fixed-array-field=16 scalar-elements=1 designated=1 holes=1 inferred-record=1'

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

"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/invalid_runtime_record_array_backward_designator.c" \
    -o "$work/invalid_runtime_record_array_backward_designator.i"
if "$minic" -S "$work/invalid_runtime_record_array_backward_designator.i" \
    -o "$work/invalid_runtime_record_array_backward_designator.s" \
    >"$work/backward.stdout" 2>"$work/backward.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/invalid_runtime_record_array_backward_designator: unexpectedly succeeded' >&2
    exit 1
fi
grep -F 'backward runtime array designators are not supported yet' \
    "$work/backward.stderr" >/dev/null
printf '%s\n' 'PASS compiler/c0/invalid_runtime_record_array_backward_designator fail-closed=1'

"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/invalid_runtime_record_array_range_designator.c" \
    -o "$work/invalid_runtime_record_array_range_designator.i"
if "$minic" -S "$work/invalid_runtime_record_array_range_designator.i" \
    -o "$work/invalid_runtime_record_array_range_designator.s" \
    >"$work/range.stdout" 2>"$work/range.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/invalid_runtime_record_array_range_designator: unexpectedly succeeded' >&2
    exit 1
fi
grep -F 'record array range designators require one element' \
    "$work/range.stderr" >/dev/null
printf '%s\n' 'PASS compiler/c0/invalid_runtime_record_array_range_designator fail-closed=1'
