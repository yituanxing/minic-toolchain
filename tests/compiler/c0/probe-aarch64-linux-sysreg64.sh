#!/usr/bin/env bash
# Pinned Linux 6.6.143 sysreg.h: read_sysreg() returns u64, not necessarily
# unsigned long. MRS is privileged; compare GNU/MiniC GAS/ELF encodings only.
# Never fake DAIF or execute EL1-only MRS under QEMU-user.
set -Eeuo pipefail
cc=${1:?MiniC compiler binary}
test -x "$cc"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat > "$work/sysreg.c" <<'SRC'
typedef unsigned long long u64;
#define read_sysreg_u64(name) ({ u64 __val; \
  asm volatile("mrs %0, " #name : "=r" (__val)); __val; })
u64 read_daif_u64(void) { return read_sysreg_u64(daif); }
unsigned long read_daif_ulong(void) {
  unsigned long v;
  asm volatile("mrs %0, daif" : "=r"(v));
  return v;
}
u64 read_sp_el0_u64(void) {
  u64 v; asm("mrs %0, sp_el0" : "=r"(v)); return v;
}
u64 read_tpidr_el1_u64(void) {
  u64 v; asm("mrs %0, tpidr_el1" : "=r"(v)); return v;
}
u64 read_tpidr_el2_u64(void) {
  u64 v; asm("mrs %0, tpidr_el2" : "=r"(v)); return v;
}
SRC
# MiniC consumes already-preprocessed GNU .i, not raw C #define directives.
cc -E -P -x c "$work/sysreg.c" -o "$work/sysreg.i"
"$cc" --target=aarch64-linux -S "$work/sysreg.i" -o "$work/mini.s"
for reg in daif sp_el0 tpidr_el1 tpidr_el2; do
  grep -Fq "  mrs x9, $reg" "$work/mini.s"
done
test "$(grep -Fc '  mrs x9, daif' "$work/mini.s")" -eq 2
# The selected type must remain a genuine 64-bit register value.
for bad in extra mnemonic width memory-clobber nonvolatile-daif; do
  case "$bad" in
    extra) sed 's/"mrs %0, daif"/"mrs %0, daif; nop"/' "$work/sysreg.c" > "$work/bad.c" ;;
    mnemonic) sed 's/"mrs %0, daif"/"mrs %0, unknown_el3"/' "$work/sysreg.c" > "$work/bad.c" ;;
    width) sed 's/u64 __val; /unsigned int __val; /' "$work/sysreg.c" > "$work/bad.c" ;;
    memory-clobber) sed 's/: "=r" (__val));/: "=r" (__val) : : "memory");/' "$work/sysreg.c" > "$work/bad.c" ;;
    nonvolatile-daif) sed 's/asm volatile("mrs %0, daif" : "=r"(v));/asm("mrs %0, daif" : "=r"(v));/' "$work/sysreg.c" > "$work/bad.c" ;;
  esac
  cc -E -P -x c "$work/bad.c" -o "$work/bad.i"
  if "$cc" --target=aarch64-linux -S "$work/bad.i" -o "$work/bad.s" > "$work/log" 2>&1; then
    echo "AARCH64_LINUX_MRS=FAIL unsafe-$bad accepted" >&2
    exit 1
  fi
  test ! -e "$work/bad.s"
done
if [[ "${MINIC_SYSREG_NATIVE_ONLY:-0}" != 1 ]]; then
  gnu=aarch64-linux-gnu-gcc
  command -v "$gnu" >/dev/null
  "$gnu" -c -O0 -x c "$work/sysreg.c" -o "$work/gnu.o"
  "$gnu" -c -x assembler "$work/mini.s" -o "$work/mini.o"
  readelf -h "$work/mini.o" | grep -Fq 'AArch64'
  aarch64-linux-gnu-objdump -d "$work/gnu.o" > "$work/gnu.dis"
  aarch64-linux-gnu-objdump -d "$work/mini.o" > "$work/mini.dis"
  for reg in daif sp_el0 tpidr_el1 tpidr_el2; do
    grep -Eiq "mrs[[:space:]]+[^,]+,[[:space:]]+$reg" "$work/gnu.dis"
    grep -Eiq "mrs[[:space:]]+[^,]+,[[:space:]]+$reg" "$work/mini.dis"
  done
fi
echo 'AARCH64_LINUX_MRS_U64=PASS original_read_sysreg_u64=1 register_forms=4 negative=5 EL1_user_exec=not_run'
