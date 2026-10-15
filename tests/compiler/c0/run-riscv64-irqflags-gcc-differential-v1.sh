#!/usr/bin/env bash
# Exact CSR instruction-family oracle modelled on Linux 6.6 arch/riscv
# asm/irqflags.h and asm/csr.h. This is a *privileged instruction static*
# check: never execute CSR_STATUS in Linux user-mode QEMU.
set -Eeuo pipefail
: "${MINIC:?MINIC is required}"
: "${BUILD_DIR:?BUILD_DIR is required}"
RISCV_CC=${RISCV_CC:-riscv64-linux-gnu-gcc}
RISCV_OBJDUMP=${RISCV_OBJDUMP:-riscv64-linux-gnu-objdump}
work="$BUILD_DIR/riscv64-irqflags-gcc-differential"
mkdir -p "$work"
cat >"$work/irqflags.c" <<'C'
/* From Linux 6.6 RISC-V: CSR_SSTATUS=0x100 and SR_SIE=0x2. */
#define CSR_STATUS 0x100
#define SR_IE 2UL
static inline unsigned long irq_save(void) {
    unsigned long v = SR_IE;
    __asm__ __volatile__("csrrc %0, 0x100, %1"
                         : "=r" (v) : "rK" (v) : "memory");
    return v;
}
static inline void irq_restore(unsigned long flags) {
    unsigned long v = flags & SR_IE;
    __asm__ __volatile__("csrs 0x100, %0"
                         : : "rK" (v) : "memory");
}
static inline void irq_disable(void) {
    unsigned long v = SR_IE;
    __asm__ __volatile__("csrc 0x100, %0"
                         : : "rK" (v) : "memory");
}
static inline void irq_enable(void) {
    unsigned long v = SR_IE;
    __asm__ __volatile__("csrs 0x100, %0"
                         : : "rK" (v) : "memory");
}
/* Strongly distinguish save, restore and enable/disable source paths. */
unsigned long irqflags_save_test(void) {
    return irq_save();
}
void irqflags_restore_test(unsigned long flags) {
    irq_restore(flags);
}
void irqflags_disable_test(void) {
    irq_disable();
}
void irqflags_enable_test(void) {
    irq_enable();
}
C
"$RISCV_CC" -E -P -x c "$work/irqflags.c" -o "$work/irqflags.i"
"$RISCV_CC" -O0 -S -x c "$work/irqflags.c" -o "$work/gcc.s"
"$MINIC" -S "$work/irqflags.i" -o "$work/minic.s"
for tool in gcc minic; do
    "$RISCV_CC" -x assembler -c "$work/$tool.s" -o "$work/$tool.o"
    "$RISCV_OBJDUMP" -d -M no-aliases "$work/$tool.o" >"$work/$tool.dis"
done
# Compare the *defining helpers*, not their non-inlined O0 wrappers.
# GNU emits all helpers in one .text and MiniC uses separate .text.<name>;
# both assembly files contain canonical name:..size boundaries.
python3 - "$work/gcc.s" "$work/minic.s" <<'PY'
import re
import sys
from pathlib import Path

def scan(path: Path) -> dict[str, list[str]]:
    source = path.read_text()
    result = {}
    for name in ("save", "restore", "disable", "enable"):
        symbol = "irq_" + name
        start = re.search(rf"(?m)^{symbol}:\s*$", source)
        if not start:
            raise SystemExit(f"IRQFLAGS_DIFF=FAIL missing_definition={symbol} file={path}")
        tail = source[start.end():]
        size = re.search(rf"(?m)^\s*\.size\s+{symbol}\s*,", tail)
        if not size:
            raise SystemExit(f"IRQFLAGS_DIFF=FAIL missing_size={symbol} file={path}")
        instructions = tail[:size.start()]
        csr = re.findall(r"(?m)^\s*(csrrc|csrs|csrc|csrrs|csrrw)\b", instructions)
        # Save/restore/enable/disable must touch the RISC-V SSTATUS CSR.
        if not re.search(r"\b0x100\b", instructions):
            raise SystemExit(f"IRQFLAGS_DIFF=FAIL missing_sstatus={symbol} file={path}")
        result[name] = csr
    return result
gnu = scan(Path(sys.argv[1]))
mini = scan(Path(sys.argv[2]))
expected = {"save": ["csrrc"], "restore": ["csrs"],
            "disable": ["csrc"], "enable": ["csrs"]}
if gnu != expected:
    raise SystemExit(f"IRQFLAGS_DIFF=INCONCLUSIVE unexpected_gcc_csr={gnu}")
if mini != expected:
    raise SystemExit(f"IRQFLAGS_DIFF=FAIL gcc={gnu} minic={mini}")
print("IRQFLAGS_GCC_MINIC=PASS csr_save=csrrc csr_restore=csrs "
      "csr_disable=csrc csr_enable=csrs compiled_ELF=both "
      "warning=static_opcode_oracle_not_proof_of_full_irq_state_runtime_semantics")
PY
