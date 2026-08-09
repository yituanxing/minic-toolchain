#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-long-types

mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/long_type_specifiers.c"     -o "$work/long_type_specifiers.i"
"$minic" -S "$work/long_type_specifiers.i" -o "$work/long_type_specifiers.s"
grep -F ".globl main" "$work/long_type_specifiers.s" >/dev/null
grep -F "identity_signed:" "$work/long_type_specifiers.s" >/dev/null
grep -F "identity_unsigned:" "$work/long_type_specifiers.s" >/dev/null
printf '%s\n' "PASS compiler/c0/long_type_specifiers"

check_invalid() {
    name=$1
    expected=$2
    "$host_cc" -E -P -x c "$root/tests/compiler/c0/$name.c" -o "$work/$name.i"
    if "$minic" -S "$work/$name.i" -o "$work/$name.s"         >"$work/$name.stdout" 2>"$work/$name.stderr"; then
        printf '%s\n' "FAIL compiler/c0/$name: compilation unexpectedly succeeded" >&2
        exit 1
    fi
    grep -F "$expected" "$work/$name.stderr" >/dev/null
    printf '%s\n' "PASS compiler/c0/$name"
}

check_invalid invalid_too_many_long_specifiers "too many long type specifiers"
check_invalid invalid_signed_unsigned "conflicting signed and unsigned type specifiers"
check_invalid invalid_long_char "char cannot be combined with short, int, or long"
