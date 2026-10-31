#!/usr/bin/env bash
# Original arm64 Linux read_sysreg GNU C template: volatile =r u64.
# These are privileged system registers: compile + GNU AS, never execute.
set -Eeuo pipefail
minic="${1:?MiniC compiler}"
test -x "$minic"
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
cat > "$d/sysreg.i" <<'SRC'
__attribute__((noinline)) unsigned long long sysreg_0(void) {
  unsigned long long word;
  asm volatile("mrs %0, " "sp_el0" : "=r" (word));
  return word;
}
__attribute__((noinline)) unsigned long long sysreg_1(void) {
  unsigned long long word;
  asm volatile("mrs %0, " "tpidr_el1" : "=r" (word));
  return word;
}
__attribute__((noinline)) unsigned long long sysreg_2(void) {
  unsigned long long word;
  asm volatile("mrs %0, " "tpidr_el2" : "=r" (word));
  return word;
}
__attribute__((noinline)) unsigned long long sysreg_3(void) {
  unsigned long long word;
  asm volatile("mrs %0, " "clidr_el1" : "=r" (word));
  return word;
}
__attribute__((noinline)) unsigned long long sysreg_4(void) {
  unsigned long long word;
  asm volatile("mrs %0, " "cntkctl_el1" : "=r" (word));
  return word;
}
__attribute__((noinline)) unsigned long long sysreg_5(void) {
  unsigned long long word;
  asm volatile("mrs %0, " "cntp_ctl_el0" : "=r" (word));
  return word;
}
__attribute__((noinline)) unsigned long long sysreg_6(void) {
  unsigned long long word;
  asm volatile("mrs %0, " "cntv_ctl_el0" : "=r" (word));
  return word;
}
__attribute__((noinline)) unsigned long long sysreg_7(void) {
  unsigned long long word;
  asm volatile("mrs %0, " "cntfrq_el0" : "=r" (word));
  return word;
}
__attribute__((noinline)) unsigned long long sysreg_8(void) {
  unsigned long long word;
  asm volatile("mrs %0, " "cntvct_el0" : "=r" (word));
  return word;
}
__attribute__((noinline)) unsigned long long sysreg_9(void) {
  unsigned long long word;
  asm volatile("mrs %0, " "tcr_el1" : "=r" (word));
  return word;
}
__attribute__((noinline)) unsigned long long sysreg_10(void) {
  unsigned long long word;
  asm volatile("mrs %0, " "esr_el1" : "=r" (word));
  return word;
}
__attribute__((noinline)) unsigned long long sysreg_11(void) {
  unsigned long long word;
  asm volatile("mrs %0, " "sctlr_el1" : "=r" (word));
  return word;
}
__attribute__((noinline)) unsigned long long sysreg_12(void) {
  unsigned long long word;
  asm volatile("mrs %0, " "far_el1" : "=r" (word));
  return word;
}
__attribute__((noinline)) unsigned long long sysreg_13(void) {
  unsigned long long word;
  asm volatile("mrs %0, " "par_el1" : "=r" (word));
  return word;
}
__attribute__((noinline)) unsigned long long sysreg_14(void) {
  unsigned long long word;
  asm volatile("mrs %0, " "id_aa64mmfr1_el1" : "=r" (word));
  return word;
}
__attribute__((noinline)) unsigned long long sysreg_15(void) {
  unsigned long long word;
  asm volatile("mrs %0, " "hcr_el2" : "=r" (word));
  return word;
}
__attribute__((noinline)) unsigned long long sysreg_16(void) {
  unsigned long long word;
  asm volatile("mrs %0, " "tpidr_el0" : "=r" (word));
  return word;
}
__attribute__((noinline)) unsigned long long sysreg_17(void) {
  unsigned long long word;
  asm volatile("mrs %0, " "mdscr_el1" : "=r" (word));
  return word;
}
__attribute__((noinline)) unsigned long long sysreg_18(void) {
  unsigned long long word;
  asm volatile("mrs %0, " "ttbr1_el1" : "=r" (word));
  return word;
}
__attribute__((noinline)) unsigned long long sysreg_19(void) {
  unsigned long long word;
  asm volatile("mrs %0, " "daif" : "=r" (word));
  return word;
}
__attribute__((noinline)) unsigned long long sysreg_20(void) {
  unsigned long long word;
  asm volatile("mrs %0, " "CurrentEL" : "=r" (word));
  return word;
}
unsigned long long nonvolatile_sysreg(void) {
 unsigned long long word;
 asm("mrs %0, sp_el0" : "=r" (word));
 return word;
}
SRC
"$minic" --target=aarch64-linux -S "$d/sysreg.i" -o "$d/mini.s"
test -s "$d/mini.s"
for reg in sp_el0 tpidr_el1 tpidr_el2 clidr_el1 cntkctl_el1 cntp_ctl_el0 cntv_ctl_el0 cntfrq_el0 cntvct_el0 tcr_el1 esr_el1 sctlr_el1 far_el1 par_el1 id_aa64mmfr1_el1 hcr_el2 tpidr_el0 mdscr_el1 ttbr1_el1 daif CurrentEL;do
 grep -Fq "mrs x9, $reg" "$d/mini.s"
done
if [[ "${MINIC_SOURCE_ONLY:-0}" != 1 ]];then
 aarch64-linux-gnu-gcc -c -x c "$d/sysreg.i" -o "$d/gnu.o"
 aarch64-linux-gnu-gcc -c -x assembler "$d/mini.s" -o "$d/mini.o"
 readelf -h "$d/mini.o" | grep -Fq 'AArch64'
 readelf -h "$d/gnu.o" | grep -Fq 'AArch64'
fi
sed 's/"clidr_el1"/"invented_el1"/' "$d/sysreg.i" > "$d/bad-register.i"
sed 's/"clidr_el1"/"clidr_el1; brk #1"/' "$d/sysreg.i" > "$d/bad-extra-instruction.i"
sed 's/: "=r" (word)/: "=m" (word)/' "$d/sysreg.i" > "$d/bad-constraint.i"
for bad in bad-register bad-extra-instruction bad-constraint;do
 if "$minic" --target=aarch64-linux -S "$d/$bad.i" -o "$d/$bad.s" >/dev/null 2>&1;then
  echo "Unsafe ARM64 read_sysreg contract was accepted: $bad" >&2
  exit 1
 fi
 test ! -e "$d/$bad.s"
done
echo 'AARCH64_ORIGINAL_LINUX_VOLATILE_MRS=PASS forms=21 GNU_GAS=1 negative_cases=3 actual_hardware_execution=NOT_RUN'
