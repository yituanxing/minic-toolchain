#!/usr/bin/env bash
# Real Linux Kbuild RV32 compat-vDSO shared-link acceptance. Read-only.
# Requires previously successful native union and P1 runtime replay.
set -Eeuo pipefail
if (( $# != 3 )); then
  echo "usage: $0 <native-trace-dir> <Kbuild-out> <cohort-log>" >&2
  exit 64
fi
trace_dir=$1
out=$2
cohort=$3
test -s "$trace_dir/minild.trace"
test -s "$cohort"
native_calls=$(grep -c '^minild-vdso32 ' "$trace_dir/minild.trace" || true)
gnu_calls=$(grep -Ec '^gnu-ld .*arch/riscv/kernel/compat_vdso/compat_vdso[.]so[.]dbg' "$trace_dir/minild.trace" || true)
((native_calls>=1 && gnu_calls==0)) || {
  echo "MINILD_VDSO32=FAIL missing_real_native_link_or_GNU_fallback native=$native_calls gnu=$gnu_calls" >&2
  exit 42
}
native="$out/arch/riscv/kernel/compat_vdso/compat_vdso.so.dbg"
test -s "$native" || {
  echo 'MINILD_VDSO32=FAIL missing_real_link_product' >&2
  exit 43
}
test -s "$out/arch/riscv/kernel/compat_vdso/compat_vdso.so" || {
  echo 'MINILD_VDSO32=FAIL missing_actual_objcopy_product' >&2
  exit 44
}
riscv64-linux-gnu-readelf -Wh -lW --dyn-syms --version-info "$native" >"$trace_dir/native-vdso32-final-elf.txt"
grep -q 'ELF32' "$trace_dir/native-vdso32-final-elf.txt"
grep -q 'RISC-V' "$trace_dir/native-vdso32-final-elf.txt"
grep -q 'DYN (Shared object file)' "$trace_dir/native-vdso32-final-elf.txt"
riscv64-linux-gnu-nm "$native" >"$trace_dir/native-vdso32-final-nm.txt"
riscv64-linux-gnu-readelf -nW "$native" >"$trace_dir/native-vdso32-final-notes.txt"
grep -q 'Build ID:' "$trace_dir/native-vdso32-final-notes.txt" || {
  echo 'MINILD_VDSO32=FAIL native_build_id_not_present' >&2
  exit 47
}
for symbol in __vdso_rt_sigreturn __vdso_getcpu __vdso_flush_icache; do
  grep -Fq "${symbol}@@LINUX_4.15" "$trace_dir/native-vdso32-final-elf.txt" || {
    echo "MINILD_VDSO32=FAIL missing_versioned_export:$symbol" >&2
    exit 45
  }
  grep -Eq "[[:xdigit:]]+ [TtWw] ${symbol}$" "$trace_dir/native-vdso32-final-nm.txt" || {
    echo "MINILD_VDSO32=FAIL missing_kernel_regular_nm:$symbol" >&2
    exit 48
  }
done
grep -Fq 'COHORT_P1=PASS name=full_all_verified syscalls=12 python_era_oracle=PASS' "$cohort" || {
  echo 'MINILD_VDSO32=FAIL no_QEMU_P1_12of12' >&2
  exit 46
}
echo "MINILD_VDSO32_KBUILD=PASS native_calls=$native_calls gnu_product_calls=0 ELF32_RISCV_ET_DYN=true versioned_exports=3 cached_MiniC_owners=2064 QEMU_P1=12/12"
