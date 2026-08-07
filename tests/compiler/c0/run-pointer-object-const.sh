#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-pointer-object-const

mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/pointer_object_const.c" \
    -o "$work/pointer_object_const.i"
"$minic" -S \
    "$work/pointer_object_const.i" \
    -o "$work/pointer_object_const.s"
grep -F ".globl main" "$work/pointer_object_const.s" >/dev/null
grep -F "  call read_value" "$work/pointer_object_const.s" >/dev/null
grep -F "  sw t0, 0(a0)" "$work/pointer_object_const.s" >/dev/null
printf '%s\n' "PASS compiler/c0/pointer_object_const"

expect_failure() {
    name=$1
    diagnostic=$2

    "$host_cc" -E -P -x c \
        "$root/tests/compiler/c0/$name.c" \
        -o "$work/$name.i"
    if "$minic" -S "$work/$name.i" -o "$work/$name.s" \
        >"$work/$name.stdout" 2>"$work/$name.stderr"; then
        printf '%s\n' \
            "FAIL compiler/c0/$name: compilation unexpectedly succeeded" >&2
        exit 1
    fi
    grep -F "$diagnostic" "$work/$name.stderr" >/dev/null
    printf '%s\n' "PASS compiler/c0/$name"
}

expect_failure \
    invalid_const_pointer_reassignment \
    "assignment target must be a modifiable lvalue"
expect_failure \
    invalid_const_pointer_parameter_reassignment \
    "assignment target must be a modifiable lvalue"
