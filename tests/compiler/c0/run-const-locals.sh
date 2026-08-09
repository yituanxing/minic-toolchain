#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-const-locals

mkdir -p "$work"

compile_success() {
    name=$1
    source=$2

    "$host_cc" -E -P -x c "$source" -o "$work/$name.i"
    "$minic" -S "$work/$name.i" -o "$work/$name.s"
}

expect_failure() {
    name=$1
    expected=$2

    "$host_cc" -E -P -x c \
        "$root/tests/compiler/c0/$name.c" \
        -o "$work/$name.i"
    if "$minic" -S "$work/$name.i" -o "$work/$name.s" \
        >"$work/$name.stdout" 2>"$work/$name.stderr"; then
        printf '%s\n' \
            "FAIL compiler/c0/$name: compilation unexpectedly succeeded" >&2
        exit 1
    fi
    grep -F "$expected" "$work/$name.stderr" >/dev/null
    printf '%s\n' "PASS compiler/c0/$name"
}

compile_success \
    const_local \
    "$root/tests/programs/c0/const_local.c"
grep -F ".type preserve_value, @function" "$work/const_local.s" >/dev/null
grep -F "  sw t0, 0(a0)" "$work/const_local.s" >/dev/null
grep -F "  lw a0, " "$work/const_local.s" >/dev/null
grep -F "  call preserve_value" "$work/const_local.s" >/dev/null
printf '%s\n' "PASS compiler/c0/const_local"

expect_failure \
    invalid_const_local_assignment \
    "assignment target must be a modifiable lvalue"
expect_failure \
    invalid_const_local_increment \
    "prefix update requires a modifiable integer or pointer lvalue"
