#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-functions

mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/static_functions.c" \
    -o "$work/static_functions.i"
"$minic" -S "$work/static_functions.i" -o "$work/static_functions.s"

grep -F ".type helper, @function" "$work/static_functions.s" >/dev/null
grep -F ".type touch, @function" "$work/static_functions.s" >/dev/null
grep -F "helper:" "$work/static_functions.s" >/dev/null
grep -F "touch:" "$work/static_functions.s" >/dev/null
grep -F "  call helper" "$work/static_functions.s" >/dev/null
grep -F "  j .Ltouch_return" "$work/static_functions.s" >/dev/null
grep -F ".globl main" "$work/static_functions.s" >/dev/null
if grep -F ".globl helper" "$work/static_functions.s" >/dev/null ||
   grep -F ".globl touch" "$work/static_functions.s" >/dev/null; then
    printf '%s\n' \
        "FAIL compiler/c0/static_functions: internal function exported" >&2
    exit 1
fi
printf '%s\n' "PASS compiler/c0/static_functions"

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
    invalid_function_linkage_conflict \
    "conflicting function declaration"
expect_failure \
    invalid_void_return_value \
    "void function cannot return a value"
expect_failure \
    invalid_int_return_without_value \
    "non-void function requires a return value"
