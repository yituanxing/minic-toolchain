#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-pointer-qualifications

mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/pointer_qualification.c" \
    -o "$work/pointer_qualification.i"
"$minic" -S \
    "$work/pointer_qualification.i" \
    -o "$work/pointer_qualification.s"
grep -F "  call read_value" "$work/pointer_qualification.s" >/dev/null
grep -F "  lw a0, 0(a0)" "$work/pointer_qualification.s" >/dev/null
printf '%s\n' "PASS compiler/c0/pointer_qualification"

expect_failure() {
    name=$1

    "$host_cc" -E -P -x c \
        "$root/tests/compiler/c0/$name.c" \
        -o "$work/$name.i"
    if "$minic" -S \
        "$work/$name.i" \
        -o "$work/$name.s" \
        >"$work/$name.stdout" \
        2>"$work/$name.stderr"; then
        printf '%s\n' \
            "FAIL compiler/c0/$name: compilation unexpectedly succeeded" >&2
        exit 1
    fi
    grep -F "call argument type does not match declaration" \
        "$work/$name.stderr" >/dev/null
    printf '%s\n' "PASS compiler/c0/$name"
}

expect_failure invalid_const_pointer_removal
expect_failure invalid_nested_const_pointer
