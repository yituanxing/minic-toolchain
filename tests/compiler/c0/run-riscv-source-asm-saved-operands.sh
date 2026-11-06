#!/bin/sh
# Pin the irq.c fixed class: 2 generic operands, all caller-saved GPRs clobbered.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cc=${HOST_CC:-cc}
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
fixture="$root/tests/compiler/c0/riscv_source_asm_saved_operands.c"
"$cc" -E -P -std=gnu11 "$fixture" -o "$work/irq.i"
"$minic" --target=riscv64-linux -march=rv64i -mabi=lp64 -S \
    "$work/irq.i" -o "$work/irq.s"
# RV64 ABI: s1=x9 and s2=x18 must be preserved in the function frame.
for physical in x9 x18; do
    grep -Eq "^[[:space:]]*sd[[:space:]]+$physical," "$work/irq.s"
    grep -Eq "^[[:space:]]*ld[[:space:]]+$physical," "$work/irq.s"
done
grep -Eq 'xor[[:space:]]+zero,[[:space:]]*x9,[[:space:]]*x18' "$work/irq.s"
"$cc" -E -P -std=gnu11 -DMINIC_BAD_NO_AVAILABLE_GPR \
    "$fixture" -o "$work/bad.i"
if "$minic" --target=riscv64-linux -march=rv64i -mabi=lp64 -S \
    "$work/bad.i" -o "$work/bad.s" >"$work/bad.stdout" 2>"$work/bad.stderr"; then
    echo 'FAIL all physical GPRs unavailable yet source ASM was accepted' >&2
    exit 1
fi
test ! -s "$work/bad.s"
echo 'RV64_SOURCE_ASM_SAVED_OPERANDS=PASS two_inputs=s1,s2 ABI_saved_and_restored=1 no_unsaved_fallback=1'
