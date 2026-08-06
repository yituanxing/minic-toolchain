#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-cast-expressions

mkdir -p "$work"

compile_success() {
    name=$1

    "$host_cc" -E -P -x c \
        "$root/tests/compiler/c0/$name.c" \
        -o "$work/$name.i"
    "$minic" -S "$work/$name.i" -o "$work/$name.s"
    printf '%s\n' "PASS compiler/c0/$name"
}

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

compile_success cast_expressions
grep -F "  slli a0, a0, 32" "$work/cast_expressions.s" >/dev/null
grep -F "  srli a0, a0, 32" "$work/cast_expressions.s" >/dev/null
grep -F "  addw a0, t0, a0" "$work/cast_expressions.s" >/dev/null
printf '%s\n' "PASS compiler/c0/cast_integer_lowering"

compile_success cast_typedef_shadow

expect_failure \
    invalid_cast_pointer_to_integer \
    "unsupported cast between these types"
expect_failure \
    invalid_cast_integer_to_pointer \
    "unsupported cast between these types"
expect_failure \
    invalid_cast_assignment_target \
    "assignment target must be a modifiable lvalue"
