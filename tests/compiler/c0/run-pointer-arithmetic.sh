#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-pointer-arithmetic

mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/programs/c0/pointer_aggregate_arithmetic.c" \
    -o "$work/pointer_aggregate_arithmetic.i"
"$minic" -S \
    "$work/pointer_aggregate_arithmetic.i" \
    -o "$work/pointer_aggregate_arithmetic.s"
grep -F "  li t1, 12" "$work/pointer_aggregate_arithmetic.s" >/dev/null
grep -F "  mul a0, a0, t1" "$work/pointer_aggregate_arithmetic.s" >/dev/null
grep -F "  mul t0, t0, t1" "$work/pointer_aggregate_arithmetic.s" >/dev/null
printf '%s\n' "PASS compiler/c0/pointer_aggregate_arithmetic"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/invalid_void_pointer_arithmetic.c" \
    -o "$work/invalid_void_pointer_arithmetic.i"
if "$minic" -S \
    "$work/invalid_void_pointer_arithmetic.i" \
    -o "$work/invalid_void_pointer_arithmetic.s" \
    >"$work/invalid_void_pointer_arithmetic.stdout" \
    2>"$work/invalid_void_pointer_arithmetic.stderr"; then
    printf '%s\n' \
        "FAIL compiler/c0/invalid_void_pointer_arithmetic: compilation unexpectedly succeeded" >&2
    exit 1
fi
grep -F "pointer arithmetic requires a complete object type" \
    "$work/invalid_void_pointer_arithmetic.stderr" >/dev/null
printf '%s\n' "PASS compiler/c0/invalid_void_pointer_arithmetic"
