#!/bin/sh
set -eu
: "${MINIC_CC:?MINIC_CC path required}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

${CC:-cc} -E -P -x c "$root/tests/compiler/c0/rv32_ffsll_high_word.c" \
    -o "$work/ffsll.i"
"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
    -S "$work/ffsll.i" -o "$work/rv32.s"
grep -Fq '_rv32_ctz64_start_' "$work/rv32.s"
grep -Fq 'mv t0, t3' "$work/rv32.s"
grep -Fq 'li t4, 32' "$work/rv32.s"

"$MINIC_CC" --target=x86_64-linux -S "$work/ffsll.i" -o "$work/x86.s"
${CC:-cc} -no-pie "$work/x86.s" -o "$work/x86"
"$work/x86"
echo 'RV32_FFSLL=PASS high_32_bit_preserved=1 x86_native_oracle=1'
