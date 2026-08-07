#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-for-loops

mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/programs/c0/for_loop.c" \
    -o "$work/for_loop.i"
"$minic" -S "$work/for_loop.i" -o "$work/for_loop.s"

grep -F ".Lwhile_condition_" "$work/for_loop.s" >/dev/null
grep -F "  sltu a0" "$work/for_loop.s" >/dev/null
grep -F "  addw a0" "$work/for_loop.s" >/dev/null
grep -F "  lwu a0" "$work/for_loop.s" >/dev/null
printf '%s\n' "PASS compiler/c0/for_loop_lowering"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/for_empty_initializer.c" \
    -o "$work/for_empty_initializer.i"
"$minic" -S "$work/for_empty_initializer.i" -o "$work/for_empty_initializer.s"
printf '%s\n' "PASS compiler/c0/for_empty_initializer"

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
    invalid_for_update \
    "postfix update requires '++' or '--'"
expect_failure \
    invalid_for_increment_target \
    "for update requires a modifiable integer or pointer local"
