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
# GNU LD on these same four verified MiniAS .S objects emits an empty
# PT_GNU_EH_FRAME, not synthetic CFI. Verify genuine native Kbuild ELF32
# preserves the empty-unwind ABI and does not invent bogus FDE pointers.
awk '$1 == "GNU_EH_FRAME" {
  seen = 1
  if ($5 != "0x00000" || $6 != "0x00000") exit 1
} END { if (!seen) exit 1 }' "$trace_dir/native-vdso32-final-elf.txt" || {
  echo 'MINILD_VDSO32=FAIL unexpected_nonempty_unwind_segment' >&2
  exit 51
}
riscv64-linux-gnu-nm "$native" >"$trace_dir/native-vdso32-final-nm.txt"
riscv64-linux-gnu-readelf -nW "$native" >"$trace_dir/native-vdso32-final-notes.txt"
grep -q 'Build ID:' "$trace_dir/native-vdso32-final-notes.txt" || {
  echo 'MINILD_VDSO32=FAIL native_build_id_not_present' >&2
  exit 47
}
grep -q 'Linux' "$trace_dir/native-vdso32-final-notes.txt" || {
  echo 'MINILD_VDSO32=FAIL native_compat_linux_note_not_present' >&2
  exit 50
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
grep -Fq 'COHORT_CANDIDATE_CACHE=HIT objects=2064' "$cohort" || {
  echo 'MINILD_VDSO32=FAIL no_attested_2064_cached_compiler_input' >&2
  exit 49
}
grep -Fq 'COHORT_P1=PASS name=full_all_verified syscalls=12 python_era_oracle=PASS' "$cohort" || {
  echo 'MINILD_VDSO32=FAIL no_QEMU_P1_12of12' >&2
  exit 46
}
# Linker-only wall time is measured from the existing Kbuild trace,
# never confused with parallel C compilation or overall QEMU time.
if [[ -s "$trace_dir/minild-times.tsv" ]]; then
  awk -F '\t' '$1=="minild-vdso32" {
    count++; wall_ms += $2
    if ($3 != 0) failed++
  } END {
    printf "MINILD_VDSO32_PERF calls=%d wall_ms_sum=%d failed=%d scope=real_Kbuild_link_only\n", count, wall_ms, failed
    if (count<1 || failed>0) exit 2
  }' "$trace_dir/minild-times.tsv"
fi
echo "MINILD_VDSO32_KBUILD=PASS native_calls=$native_calls gnu_product_calls=0 ELF32_RISCV_ET_DYN=true versioned_exports=3 cached_MiniC_owners=2064 QEMU_P1=12/12"
