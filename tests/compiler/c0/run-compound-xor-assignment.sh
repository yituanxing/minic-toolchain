#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-compound-xor-assignment

mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/programs/c0/compound_xor_assignment.c" \
    -o "$work/compound_xor_assignment.i"
"$minic" -S \
    "$work/compound_xor_assignment.i" \
    -o "$work/compound_xor_assignment.s"

call_count=$(grep -c -F "  call pick" "$work/compound_xor_assignment.s" || true)
if test "$call_count" -ne 1; then
    printf '%s\n' \
        "FAIL compiler/c0/compound_xor_assignment: target call count=$call_count expected=1" >&2
    exit 1
fi
grep -F "  xor a0, t0, a0" "$work/compound_xor_assignment.s" >/dev/null
grep -F "  sw a0, 0(t0)" "$work/compound_xor_assignment.s" >/dev/null
printf '%s\n' "PASS compiler/c0/compound_xor_assignment"

check_invalid() {
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

check_invalid \
    invalid_compound_xor_pointer_target \
    "compound XOR assignment requires integer operands"
check_invalid \
    invalid_compound_xor_pointer_rhs \
    "compound XOR assignment requires integer operands"
check_invalid \
    invalid_const_compound_xor \
    "assignment target must be a modifiable lvalue"
