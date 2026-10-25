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
"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
     -S "$work/unit.i" -o "$work/rv32.s"
test -s "$work/rv32.s"
grep -q '^f:' "$work/rv32.s"
if grep -Eq '^[[:space:]]+(ld|sd|addiw|slliw|srliw|sraiw|addw|subw|mul)[[:space:]]' \
   "$work/rv32.s"; then
    echo 'RV32I_TARGET_GATE=FAIL emitted_illegal_opcode' >&2; exit 1
fi
if "$MINIC_CC" --target=riscv32-linux -march=rv64i -mabi=lp64 \
     -S "$work/unit.i" -o "$work/mixed.s" 2>"$work/reject.log"; then
    echo 'RV32I_TARGET_GATE=FAIL accepted_RV64_profile' >&2; exit 1
fi
grep -F 'invalid RISC-V configuration' "$work/reject.log"
test ! -e "$work/mixed.s"
cat > "$work/real-kernel-fixed.i" <<'SRC'
struct task_struct;
register struct task_struct *riscv_current_is_tp __asm__("tp");
register unsigned long current_stack_pointer __asm__("sp");
struct task_struct *rv32_current(void) { return riscv_current_is_tp; }
unsigned long rv32_stack_pointer(void) { return current_stack_pointer; }
SRC
"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
    -S "$work/real-kernel-fixed.i" -o "$work/real-kernel-fixed.s"
grep -Fq 'mv t0, tp' "$work/real-kernel-fixed.s"
grep -Fq 'mv t0, sp' "$work/real-kernel-fixed.s"
cat > "$work/unsafe-fixed.i" <<'SRC'
register unsigned long unsafe_argument_register __asm__("a0");
unsigned long unsafe(void) { return unsafe_argument_register; }
SRC
if "$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
   -S "$work/unsafe-fixed.i" -o "$work/unsafe-fixed.s" 2>"$work/unsafe.log"; then
    echo 'RV32I_FIXED_REGISTER=FAIL accepted_live_argument_register' >&2
    exit 1
fi
test ! -e "$work/unsafe-fixed.s"
echo 'RV32I_FIXED_REGISTER=PASS tp_sp=Core_Target_RV32I bad_a0_rejected=1'
echo 'RV32I_TARGET_GATE=PASS ILP32_registered=true incompatible_ISA_rejected=true basic_C_core_emitted=true'
