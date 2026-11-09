#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/aarch64-neon-stage3-strict
source_file="$root/tests/compiler/c0/aarch64_neon_sha3_eor3_contract.c"
mkdir -p "$work"

gcc -E -P -x c "$source_file" -o "$work/sha3_contract_good.i"
"$minic_cc" --target=aarch64-linux -S "$work/sha3_contract_good.i" \
    -o "$work/sha3_contract_good.s" 2>"$work/sha3_contract_good.stderr"
if ! grep -q '^eor3 v16\.16b, v17\.16b, v18\.16b, v19\.16b$' \
        "$work/sha3_contract_good.s"; then
    printf '%s\n' 'FAIL AArch64 EOR3 lost distinct Q registers or lane suffix' >&2
    exit 1
fi
if ! grep -q '^  str q16, \[x14, #0\]$' "$work/sha3_contract_good.s"; then
    printf '%s\n' 'FAIL AArch64 EOR3 lost its typed Q output writeback' >&2
    exit 1
fi

for bad in WIDTH GNU_VECTOR LANE TEMPLATE VOLATILE CONSTRAINT OUTPUT_CONSTRAINT; do
    gcc -E -P -x c -D"SHA3_BAD_$bad" "$source_file" \
        -o "$work/sha3_contract_bad_$bad.i"
    if "$minic_cc" --target=aarch64-linux -S \
            "$work/sha3_contract_bad_$bad.i" \
            -o "$work/sha3_contract_bad_$bad.s" \
            >"$work/sha3_contract_bad_$bad.stdout" \
            2>"$work/sha3_contract_bad_$bad.stderr"; then
        printf '%s\n' "FAIL AArch64 EOR3 admitted invalid source: $bad" >&2
        exit 1
    fi
done
for foreign in x86_64-linux riscv64-linux; do
    if "$minic_cc" --target="$foreign" -S "$work/sha3_contract_good.i" \
            -o "$work/sha3_contract_foreign_$foreign.s" \
            >"$work/sha3_contract_foreign_$foreign.stdout" \
            2>"$work/sha3_contract_foreign_$foreign.stderr"; then
        printf '%s\n' "FAIL SHA3 NEON Q asm leaked into $foreign" >&2
        exit 1
    fi
done
printf '%s\n' 'PASS AArch64 GNU SHA3 EOR3 vector Q operand contract (1 positive, 9 negatives)'
