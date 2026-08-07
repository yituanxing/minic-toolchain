#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-local-arrays

mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/programs/c0/static_local_array.c" \
    -o "$work/static_local_array.i"
"$minic" -S "$work/static_local_array.i" -o "$work/static_local_array.s"

grep -F "__minic_static_local_" "$work/static_local_array.s" >/dev/null
grep -F ".zero 4" "$work/static_local_array.s" >/dev/null
grep -F ".zero 15" "$work/static_local_array.s" >/dev/null
if grep -F ".globl __minic_static_local_" "$work/static_local_array.s" >/dev/null; then
    printf '%s\n' "FAIL compiler/c0/static_local_array: hidden static object exported" >&2
    exit 1
fi
printf '%s\n' "PASS compiler/c0/static_local_array"

expect_failure() {
    name=$1
    message=$2

    "$host_cc" -E -P -x c \
        "$root/tests/compiler/c0/$name.c" \
        -o "$work/$name.i"
    if "$minic" -S "$work/$name.i" -o "$work/$name.s" \
        >"$work/$name.stdout" 2>"$work/$name.stderr"; then
        printf '%s\n' \
            "FAIL compiler/c0/$name: compilation unexpectedly succeeded" >&2
        exit 1
    fi
    grep -F "$message" "$work/$name.stderr" >/dev/null
    printf '%s\n' "PASS compiler/c0/$name"
}

expect_failure \
    invalid_static_local_scalar \
    "static local object currently requires a fixed array declarator"
expect_failure \
    invalid_static_local_initializer \
    "static local initializers are not supported yet"
expect_failure \
    invalid_static_local_duplicate \
    "duplicate local declaration"
