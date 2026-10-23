#!/bin/sh
# RV32 target selection is real, but only the semantic model is qualified.
# Prevent accidentally running the RV64 emitter for ILP32 C code.
set -eu
: "${MINIC_CC:?MINIC_CC path required}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
printf '%s\n' 'int f(int x) { return x+1; }' > "$work/unit.i"
"$MINIC_CC" --target=riscv64-linux -S "$work/unit.i" -o "$work/rv64.s"
test -s "$work/rv64.s"
if "$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
     -S "$work/unit.i" -o "$work/rv32.s" 2>"$work/reject.log"; then
    echo 'RV32I_TARGET_GATE=FAIL accepted_unqualified_RV64_emitter' >&2; exit 1
fi
grep -F 'machine/ABI lowering is not yet qualified' "$work/reject.log"
test ! -e "$work/rv32.s"
if "$MINIC_CC" --target=riscv32-linux -march=rv64i -mabi=lp64 \
     -S "$work/unit.i" -o "$work/mixed.s" 2>"$work/reject.log"; then
    echo 'RV32I_TARGET_GATE=FAIL accepted_RV64_profile' >&2; exit 1
fi
grep -F 'invalid RISC-V configuration' "$work/reject.log"
test ! -e "$work/mixed.s"
echo 'RV32I_TARGET_GATE=PASS ILP32_registered=true incompatible_ISA_rejected=true unsafe_machine_emission_rejected=true'
