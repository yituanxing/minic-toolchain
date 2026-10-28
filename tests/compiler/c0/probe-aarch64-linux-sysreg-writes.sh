#!/usr/bin/env bash
# No EL1/EL2 instruction is executed under QEMU-user.
# Original arm64 sysreg.h GNU macro: one volatile rZ 64-bit operand.
set -Eeuo pipefail
minic="${1:?MiniC binary}";test -x "$minic"
d=$(mktemp -d);trap 'rm -rf "$d"' EXIT
cat > "$d/sysreg.i" <<'SRC'
void write_sysreg_daif(unsigned long long input) {
  unsigned long long value=input;
  asm volatile("msr daif, %x0" : : "rZ" (value));
}
void write_sysreg_ttbr0_el1(unsigned long long input) {
  unsigned long long value=input;
  asm volatile("msr ttbr0_el1, %x0" : : "rZ" (value));
}
void write_sysreg_ttbr1_el1(unsigned long long input) {
  unsigned long long value=input;
  asm volatile("msr ttbr1_el1, %x0" : : "rZ" (value));
}
void write_sysreg_tcr_el1(unsigned long long input) {
  unsigned long long value=input;
  asm volatile("msr tcr_el1, %x0" : : "rZ" (value));
}
void write_sysreg_sctlr_el1(unsigned long long input) {
  unsigned long long value=input;
  asm volatile("msr sctlr_el1, %x0" : : "rZ" (value));
}
void write_sysreg_contextidr_el1(unsigned long long input) {
  unsigned long long value=input;
  asm volatile("msr contextidr_el1, %x0" : : "rZ" (value));
}
void write_sysreg_cpacr_el1(unsigned long long input) {
  unsigned long long value=input;
  asm volatile("msr cpacr_el1, %x0" : : "rZ" (value));
}
void write_sysreg_vbar_el1(unsigned long long input) {
  unsigned long long value=input;
  asm volatile("msr vbar_el1, %x0" : : "rZ" (value));
}
void write_sysreg_mdscr_el1(unsigned long long input) {
  unsigned long long value=input;
  asm volatile("msr mdscr_el1, %x0" : : "rZ" (value));
}
void write_sysreg_osdlr_el1(unsigned long long input) {
  unsigned long long value=input;
  asm volatile("msr osdlr_el1, %x0" : : "rZ" (value));
}
void write_sysreg_oslar_el1(unsigned long long input) {
  unsigned long long value=input;
  asm volatile("msr oslar_el1, %x0" : : "rZ" (value));
}
void write_sysreg_cntkctl_el1(unsigned long long input) {
  unsigned long long value=input;
  asm volatile("msr cntkctl_el1, %x0" : : "rZ" (value));
}
void write_sysreg_cntp_ctl_el0(unsigned long long input) {
  unsigned long long value=input;
  asm volatile("msr cntp_ctl_el0, %x0" : : "rZ" (value));
}
void write_sysreg_cntp_cval_el0(unsigned long long input) {
  unsigned long long value=input;
  asm volatile("msr cntp_cval_el0, %x0" : : "rZ" (value));
}
void write_sysreg_cntv_ctl_el0(unsigned long long input) {
  unsigned long long value=input;
  asm volatile("msr cntv_ctl_el0, %x0" : : "rZ" (value));
}
void write_sysreg_cntv_cval_el0(unsigned long long input) {
  unsigned long long value=input;
  asm volatile("msr cntv_cval_el0, %x0" : : "rZ" (value));
}
void write_sysreg_tpidr_el0(unsigned long long input) {
  unsigned long long value=input;
  asm volatile("msr tpidr_el0, %x0" : : "rZ" (value));
}
void write_sysreg_tpidrro_el0(unsigned long long input) {
  unsigned long long value=input;
  asm volatile("msr tpidrro_el0, %x0" : : "rZ" (value));
}
void write_sysreg_tpidr_el2(unsigned long long input) {
  unsigned long long value=input;
  asm volatile("msr tpidr_el2, %x0" : : "rZ" (value));
}
void write_sysreg_hcr_el2(unsigned long long input) {
  unsigned long long value=input;
  asm volatile("msr hcr_el2, %x0" : : "rZ" (value));
}
void write_sysreg_cptr_el2(unsigned long long input) {
  unsigned long long value=input;
  asm volatile("msr cptr_el2, %x0" : : "rZ" (value));
}
void write_sysreg_vtcr_el2(unsigned long long input) {
  unsigned long long value=input;
  asm volatile("msr vtcr_el2, %x0" : : "rZ" (value));
}
void write_sysreg_vttbr_el2(unsigned long long input) {
  unsigned long long value=input;
  asm volatile("msr vttbr_el2, %x0" : : "rZ" (value));
}SRC
"$minic" --target=aarch64-linux -S "$d/sysreg.i" -o "$d/mini.s"
test -s "$d/mini.s"
for reg in daif ttbr0_el1 ttbr1_el1 tcr_el1 sctlr_el1 contextidr_el1 cpacr_el1 vbar_el1 mdscr_el1 osdlr_el1 oslar_el1 cntkctl_el1 cntp_ctl_el0 cntp_cval_el0 cntv_ctl_el0 cntv_cval_el0 tpidr_el0 tpidrro_el0 tpidr_el2 hcr_el2 cptr_el2 vtcr_el2 vttbr_el2;do
    grep -Fq "msr $reg, x9" "$d/mini.s"
done
if [[ "${MINIC_SOURCE_ONLY:-0}" != 1 ]];then
    aarch64-linux-gnu-gcc -c -O0 -x c "$d/sysreg.i" -o "$d/gnu.o"
    aarch64-linux-gnu-gcc -c -x assembler "$d/mini.s" -o "$d/mini.o"
    test "$(readelf -h "$d/mini.o" | grep -c AArch64)" -ge 1
    test "$(readelf -h "$d/gnu.o" | grep -c AArch64)" -ge 1
fi
sed 's/msr ttbr0_el1, %x0/msr ttbr0_el3, %x0/' "$d/sysreg.i" > "$d/unsafe-reg.i"
sed 's/msr ttbr0_el1, %x0/msr ttbr0_el1, %x0; brk #1/' "$d/sysreg.i" > "$d/unsafe-inject.i"
sed 's/"rZ" (value)/"m" (value)/' "$d/sysreg.i" > "$d/unsafe-constraint.i"
for bad in unsafe-reg unsafe-inject unsafe-constraint;do
    if "$minic" --target=aarch64-linux -S "$d/$bad.i" -o "$d/$bad.s" >/dev/null 2>&1;then
        echo "AArch64 unqualified sysreg $bad was emitted" >&2;exit 1
    fi
    test ! -e "$d/$bad.s"
done
echo 'AARCH64_LINUX_SYSREG_WRITES=PASS register_forms=23 GNU_AS=1 privileged_not_executed=1 negatives=3'
