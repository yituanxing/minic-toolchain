#!/usr/bin/env bash
# Exercise the actual GNU C→Core owned-name ingress, not handcrafted MachineIR.
set -Eeuo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
: "${MINIC_CC:?MINIC_CC path required}"
host_cc=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

"$host_cc" -E -P -nostdinc -std=gnu11 -x c \
    "$root/tests/compiler/c0/riscv_named_asm_goto.c" -o "$work/real.i"
for arch in rv32 rv64; do
    if [ "$arch" = rv32 ]; then
        target=riscv32-linux march=rv32i abi=ilp32
    else
        target=riscv64-linux march=rv64i abi=lp64
    fi
    for failure in 1 2 3; do
        "$host_cc" -E -P -nostdinc -std=gnu11 -x c \
            -DNAMED_GOTO_FAILURE="$failure" \
            "$root/tests/compiler/c0/riscv_named_asm_goto_failures.c" \
            -o "$work/forged-$failure.i"
        if "$MINIC_CC" --target="$target" -march="$march" -mabi="$abi" \
            -S "$work/forged-$failure.i" -o "$work/forged-$failure.s" \
            2>"$work/forged-$failure.log"; then
            echo "RISCV_NAMED_ASM_GOTO=FAIL accepted_forge arch=$arch case=$failure" >&2
            exit 1
        fi
        if [ -e "$work/forged-$failure.s" ]; then
            echo "RISCV_NAMED_ASM_GOTO=FAIL emitted_failed_source arch=$arch case=$failure" >&2
            exit 1
        fi
    done
    "$MINIC_CC" --target="$target" -march="$march" -mabi="$abi" \
        -S "$work/real.i" -o "$work/$arch.s"
    grep -Fq '.word 11' "$work/$arch.s"
    grep -Fq '.word 3, 5, 7' "$work/$arch.s"
    grep -Eq '\.long 1b - \., \.Luser_[0-9]+ - \.' "$work/$arch.s"
    if grep -Eq '%\[key\]|%l\[enabled\]|%\[alpha\]|%l\[yes\]' \
        "$work/$arch.s"; then
        echo "RISCV_NAMED_ASM_GOTO=FAIL unbound source name arch=$arch" >&2
        exit 1
    fi
done

echo 'RISCV_NAMED_ASM_GOTO=PASS RV32+RV64 original_named_one_and_two_label=4 missing/wrong-kind/collision_rejected=6 Core_owner=1'
