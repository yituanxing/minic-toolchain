#!/bin/sh
# Regression for actual io_uring/net.c and fbcon.c GNU overflow operand shapes.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

edge="$root/tests/compiler/c0/riscv_mixed_checked_overflow_edge.c"
"$host_cc" -std=gnu11 -O2 -Wall -Wextra "$edge" -o "$work/edge-host"
"$work/edge-host"

for fixture in riscv_mixed_checked_overflow_edge gnu_overflow_builtins mixed-overflow-u64-i32; do
    "$host_cc" -E -P -std=gnu11 -x c \
        "$root/tests/compiler/c0/$fixture.c" -o "$work/$fixture.i"
    "$minic" --target=riscv64-linux -march=rv64i -mabi=lp64 \
        -S "$work/$fixture.i" -o "$work/$fixture-rv64.s"
    "$minic" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
        -S "$work/$fixture.i" -o "$work/$fixture-rv32.s"
    test -s "$work/$fixture-rv64.s"
    test -s "$work/$fixture-rv32.s"
done
grep -F '__muldi3' "$work/riscv_mixed_checked_overflow_edge-rv64.s" >/dev/null
grep -F 'wide_first:' "$work/riscv_mixed_checked_overflow_edge-rv64.s" >/dev/null
grep -F 'signed_first:' "$work/riscv_mixed_checked_overflow_edge-rv64.s" >/dev/null
grep -F 'multiply_signed_first:' "$work/riscv_mixed_checked_overflow_edge-rv64.s" >/dev/null
grep -F 'multiply_unsigned_first:' "$work/riscv_mixed_checked_overflow_edge-rv64.s" >/dev/null

# This has not acquired the 128-bit signed64+signed32 proof and must not be
# silently accepted as unsigned64+signed32 arithmetic.
"$host_cc" -E -P -std=gnu11 -DMIXED_UNSUPPORTED=1 \
    "$root/tests/compiler/c0/mixed-overflow-u64-i32.c" \
    -o "$work/unsupported.i"
if "$minic" --target=riscv64-linux -march=rv64i -mabi=lp64 \
    -S "$work/unsupported.i" -o "$work/unsupported.s" \
    >"$work/rejected.out" 2>"$work/rejected.err"; then
    echo 'RV64_MIXED_CHECKED_OVERFLOW=FAIL unsupported signed64 input admitted' >&2
    exit 1
fi
echo 'RV64_MIXED_CHECKED_OVERFLOW=PASS host_boundaries=11 RV32_RV64_sources=3 mixed_add_mul=1 fail_closed=1'
