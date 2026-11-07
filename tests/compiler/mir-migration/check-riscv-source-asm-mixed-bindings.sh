#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
test -x "$minic"

work=$(mktemp -d "${TMPDIR:-/tmp}/minic-riscv-source-asm.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

# These are original GNU C regression fixtures, not generated ASM stubs.
# Every result has to survive Core -> selected physical MachineIR -> GNU .s.
for fixture in gnu_inline_asm_eight_operands \
    gnu_inline_asm_readwrite_earlyclobber gnu_inline_asm_operands; do
    "$host_cc" -E -P -std=gnu11 -x c \
        "$root/tests/compiler/c0/$fixture.c" -o "$work/$fixture.i"
    for spec in 'riscv32-linux rv32i ilp32' \
        'riscv64-linux rv64i lp64' 'riscv64-linux rv64imafd lp64d'; do
        set -- $spec
        "$minic" --target="$1" -march="$2" -mabi="$3" -S \
            "$work/$fixture.i" -o "$work/$fixture.$3.s"
        test -s "$work/$fixture.$3.s"
        grep -F '# MINIC_' "$work/$fixture.$3.s" >/dev/null
        if grep -F '# MINIC_RV64I_ASM_BEGIN' "$work/$fixture.$3.s" \
                >/dev/null && test "$1" = riscv32-linux; then
            echo 'RV32 source ASM forged RV64 marker' >&2
            exit 1
        fi
    done
done

# GNU +&r early-clobber operands use distinct physical x-registers, and
# original fixed a0/a1 inputs/outputs retain their exact architectural IDs.
grep -F 'add x10, x10, x13' \
    "$work/gnu_inline_asm_readwrite_earlyclobber.lp64d.s" >/dev/null
grep -F 'add x11, x11, x13' \
    "$work/gnu_inline_asm_readwrite_earlyclobber.lp64d.s" >/dev/null
grep -F 'addi t3, zero, 7' \
    "$work/gnu_inline_asm_operands.lp64d.s" >/dev/null

# Core has sixteen structured-operand slots; seventeen source register
# inputs must stay rejected before any text/emission escape can occur.
"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/gnu_inline_asm_over_capacity.c" \
    -o "$work/over-capacity.i"
if "$minic" --target=riscv64-linux -march=rv64i -mabi=lp64 \
        -S "$work/over-capacity.i" -o "$work/over-capacity.s" \
        >"$work/over-capacity.stdout" 2>"$work/over-capacity.stderr"; then
    echo 'Seventeen GNU source operands unexpectedly admitted' >&2
    exit 1
fi
test ! -s "$work/over-capacity.s"

echo 'PASS riscv-source-asm-mixed-bindings original=3 targets=3 success=9 over-capacity=reject'
