#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-integer-bit-operations

mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/integer_bit_operations.c" \
    -o "$work/integer_bit_operations.i"
"$minic" -S \
    "$work/integer_bit_operations.i" \
    -o "$work/integer_bit_operations.s"
grep -F "  sllw a0, t0, a0" "$work/integer_bit_operations.s" >/dev/null
grep -F "  sraw a0, t0, a0" "$work/integer_bit_operations.s" >/dev/null
grep -F "  srlw a0, t0, a0" "$work/integer_bit_operations.s" >/dev/null
grep -F "  and a0, t0, a0" "$work/integer_bit_operations.s" >/dev/null
printf '%s\n' "PASS compiler/c0/integer_bit_operations"

expect_failure() {
    name=$1

    "$host_cc" -E -P -x c \
        "$root/tests/compiler/c0/$name.c" \
        -o "$work/$name.i"
    if "$minic" -S "$work/$name.i" -o "$work/$name.s" \
        >"$work/$name.stdout" 2>"$work/$name.stderr"; then
        printf '%s\n' \
            "FAIL compiler/c0/$name: compilation unexpectedly succeeded" >&2
        exit 1
    fi
    grep -F "binary operator requires int operands" \
        "$work/$name.stderr" >/dev/null
    printf '%s\n' "PASS compiler/c0/$name"
}

expect_failure invalid_shift_pointer
expect_failure invalid_bitwise_and_pointer
