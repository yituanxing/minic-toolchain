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
python3 - "$work/gcc.dis" "$work/minic.dis" <<'PY'
import re
import sys
from pathlib import Path

def scan(path: Path) -> dict[str, list[str]]:
    code = path.read_text()
    actual = {}
    for name in ("save", "restore", "disable", "enable"):
        symbol = "irqflags_" + name + "_test"
        m = re.search(rf"(?m)^[0-9a-f]+ <{symbol}>:\s*$", code)
        if not m:
            raise SystemExit(f"IRQFLAGS_DIFF=FAIL missing_symbol={symbol} file={path}")
        remainder = code[m.end():]
        following = re.search(r"(?m)^[0-9a-f]+ <[^>]+>:\s*$", remainder)
        body = remainder[:following.start()] if following else remainder
        csr = re.findall(r"\b(csrrc|csrrs|csrrw)\b[^\n]*\bsstatus\b", body)
        if not csr:
            # Some objdump versions display the CSR number rather than 'sstatus'.
            csr = re.findall(r"\b(csrrc|csrrs|csrrw)\b[^\n]*\b0x100\b", body)
        actual[name] = csr
    return actual
gnu = scan(Path(sys.argv[1]))
mini = scan(Path(sys.argv[2]))
expected = {"save": ["csrrc"], "restore": ["csrrs"],
            "disable": ["csrrc"], "enable": ["csrrs"]}
if gnu != expected:
    raise SystemExit(f"IRQFLAGS_DIFF=INCONCLUSIVE unexpected_gcc_csr={gnu}")
if mini != expected:
    raise SystemExit(f"IRQFLAGS_DIFF=FAIL gcc={gnu} minic={mini}")
print("IRQFLAGS_GCC_MINIC=PASS csr_save=csrrc csr_restore=csrrs "
      "csr_disable=csrrc csr_enable=csrrs codegen_compiled=both "
      "warning=does_not_prove_full_irq_state_runtime_semantics")
PY
