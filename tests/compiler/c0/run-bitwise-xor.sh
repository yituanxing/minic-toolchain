#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-bitwise-xor

mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/programs/c0/bitwise_xor.c" \
    -o "$work/bitwise_xor.i"
"$minic" -S "$work/bitwise_xor.i" -o "$work/bitwise_xor.s"
grep -F ".Lmain_core_bb" "$work/bitwise_xor.s" >/dev/null
grep -F ".Lmain_core_return:" "$work/bitwise_xor.s" >/dev/null
# Preserve the operation-level contract without pinning Core's temporary
# register allocation.  The source exercises XOR, equality normalization and
# unsigned remainder in one value flow.
grep -E '^[[:space:]]+xor[[:space:]]+' "$work/bitwise_xor.s" >/dev/null
grep -E '^[[:space:]]+seqz[[:space:]]+' "$work/bitwise_xor.s" >/dev/null
grep -E '^[[:space:]]+remuw[[:space:]]+' "$work/bitwise_xor.s" >/dev/null
printf '%s\n' "PASS compiler/c0/bitwise_xor normalized=core-integer-ops"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/invalid_bitwise_xor_pointer.c" \
    -o "$work/invalid_bitwise_xor_pointer.i"
if "$minic" -S \
    "$work/invalid_bitwise_xor_pointer.i" \
    -o "$work/invalid_bitwise_xor_pointer.s" \
    >"$work/invalid_bitwise_xor_pointer.stdout" \
    2>"$work/invalid_bitwise_xor_pointer.stderr"; then
    printf '%s\n' \
        "FAIL compiler/c0/invalid_bitwise_xor_pointer: compilation unexpectedly succeeded" >&2
    exit 1
fi
grep -F "binary operator requires int operands" \
    "$work/invalid_bitwise_xor_pointer.stderr" >/dev/null
printf '%s\n' "PASS compiler/c0/invalid_bitwise_xor_pointer"
