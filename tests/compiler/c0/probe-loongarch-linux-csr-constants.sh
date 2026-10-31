#!/usr/bin/env bash
# Original GNU 6.6.143 arch/loongarch uses __builtin_loongarch_csrrd_{w,d}
# inside genuine source after the generic Core scalar argument snapshot.
# Kernel CSR accesses are privileged; test target *assembly*, not user QEMU.
set -Eeuo pipefail
mini=${1:?MiniC compiler}
test -x "$mini"
d=$(mktemp -d);trap 'rm -rf "$d"' EXIT
cat > "$d/csr.i" <<'SRC'
unsigned int linux_read_csr0(void) { return __builtin_loongarch_csrrd_w(0x20); }
unsigned long linux_read_csr1(void) { return __builtin_loongarch_csrrd_d(0x10); }
unsigned long linux_write_csr(unsigned long x) {
    return __builtin_loongarch_csrwr_d(x, 0x10);
}
unsigned long linux_exchange_csr(unsigned long value, unsigned long mask) {
    return __builtin_loongarch_csrxchg_d(value, mask, 0x10);
}
unsigned int linux_csr_compound(void) {
    return __builtin_loongarch_csrrd_w((unsigned int)(0x10 + 0x10));
}
SRC
"$mini" --target=loongarch64-linux -S "$d/csr.i" -o "$d/csr.s"
grep -Fq 'csrrd $a0, 32' "$d/csr.s"
grep -Fq 'csrrd $a0, 16' "$d/csr.s"
grep -Fq 'csrwr $a0, 16' "$d/csr.s"
grep -Fq 'csrxchg $a0, $a1, 16' "$d/csr.s"
test "$(grep -Fc 'csrrd $a0, 32' "$d/csr.s")" -eq 2
for bad in dynamic overrange unknown;do
  case "$bad" in
    dynamic) cat > "$d/bad.i" <<'SRC'
unsigned int f(unsigned int csr){return __builtin_loongarch_csrrd_w(csr);}
SRC
      ;;
    overrange) cat > "$d/bad.i" <<'SRC'
unsigned int f(void){return __builtin_loongarch_csrrd_w(16384);}
SRC
      ;;
    unknown) cat > "$d/bad.i" <<'SRC'
unsigned int f(void){return __builtin_loongarch_unknown_csr(32);}
SRC
      ;;
  esac
  if "$mini" --target=loongarch64-linux -S "$d/bad.i" -o "$d/bad.s" >"$d/err" 2>&1;then
    echo "INVALID_LOONGARCH_CSR_ACCEPTED=$bad" >&2;exit 1
  fi
  test ! -e "$d/bad.s"
done
if [[ "${MINIC_LOONG_CSR_SOURCE_ONLY:-0}" != 1 ]]; then
  command -v loongarch64-linux-gnu-gcc-13 >/dev/null
  loongarch64-linux-gnu-gcc-13 -c -x assembler "$d/csr.s" -o "$d/csr.o"
  loongarch64-linux-gnu-readelf -h "$d/csr.o" | grep -Fq 'LoongArch'
fi
echo "MINIC_LOONGARCH_CSR_CONST=PASS genuine_linux=1 forms=4 source_snapshot=1 negatives=3 GAS_mode=${MINIC_LOONG_CSR_SOURCE_ONLY:-0}"
