#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
. "$root/tests/compiler/c0/machine-cfg-contract.sh"
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-bitwise-xor

mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/programs/c0/bitwise_xor.c" \
    -o "$work/bitwise_xor.i"
"$minic" -S "$work/bitwise_xor.i" -o "$work/bitwise_xor.s"
require_machine_function_cfg "$work/bitwise_xor.s" main 2
# Core emits XLEN integer operations and then applies the result conversion;
# verify signedness-sensitive semantics without pinning the legacy W-form.
grep -E '^[[:space:]]+xor[[:space:]]+' "$work/bitwise_xor.s" >/dev/null
grep -E '^[[:space:]]+seqz[[:space:]]+' "$work/bitwise_xor.s" >/dev/null
# RV64I has no M extension.  Unsigned modulo is either an M-extension REMU
# or the psABI compiler-rt helper; both preserve the original operation.
if ! grep -E '^[[:space:]]+remu[[:space:]]+' "$work/bitwise_xor.s" >/dev/null; then
    grep -E '^[[:space:]]+call[[:space:]]+__umoddi3$' "$work/bitwise_xor.s" >/dev/null
fi
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
