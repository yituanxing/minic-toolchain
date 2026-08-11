#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
build_dir=${BUILD_DIR:-"$root/build/debug"}
work="$build_dir/tests/compiler-c0-static-local-arrays"

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

MINIC="$minic" \
HOST_CC="$host_cc" \
BUILD_DIR="$build_dir" \
sh "$root/tests/compiler/c0/run-static-local-scalars.sh"

MINIC="$minic" \
HOST_CC="$host_cc" \
BUILD_DIR="$build_dir" \
sh "$root/tests/compiler/c0/run-static-local-fixed-arrays.sh"

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
    if ! grep -F "$message" "$work/$name.stderr" >/dev/null; then
        printf '%s\n' "FAIL compiler/c0/$name: diagnostic mismatch" >&2
        cat "$work/$name.stderr" >&2
        exit 1
    fi
    printf '%s\n' "PASS compiler/c0/$name"
}

expect_failure \
    invalid_static_local_duplicate \
    "duplicate local declaration"
