#!/usr/bin/env bash
# RV32I entry into the ALREADY established Linux 6.6.143 Kbuild acceptance.
# This qualifies the original GNU RV32 kernel build first, then exactly five
# authentic Kbuild C owners via the existing GNU-E -> MiniC-S -> GNU-AS proxy.
# NOT a seven-shard/full-MiniC/QEMU P1 certification until those gates run.
set -Eeuo pipefail
: "${MINIC_CC:?required current-main MiniC}"
: "${RV32I_LINUX_SOURCE:?required pinned original 6.6.143 source cache}"
: "${RV32I_LINUX_WORK:?required isolated evidence dir}"
src=$(realpath "$RV32I_LINUX_SOURCE")
test -f "$src/Makefile"
test -x "$MINIC_CC"
mkdir -p "$RV32I_LINUX_WORK"
out="$RV32I_LINUX_WORK/out"
ev="$RV32I_LINUX_WORK/evidence"
mkdir -p "$out" "$ev"
cd "$RV32I_LINUX_WORK"
export LC_ALL=C
cross=riscv64-linux-gnu-
gnu_args=(-C "$src" O="$out" ARCH=riscv CROSS_COMPILE="$cross")
tools=(make riscv64-linux-gnu-gcc riscv64-linux-gnu-as riscv64-linux-gnu-ld riscv64-linux-gnu-readelf sha256sum)
for tool in "${tools[@]}"; do command -v "$tool" >/dev/null; done
actual_version=$(make "${gnu_args[@]}" -s kernelversion)
if [[ "$actual_version" != 6.6.143 ]]; then
  echo "RV32I_LINUX=FAIL unexpected_kernel=$actual_version expected=6.6.143" >&2
  exit 2
fi
make "${gnu_args[@]}" defconfig >"$ev/defconfig.log" 2>&1
"$src/scripts/config" --file "$out/.config" \
  -e NONPORTABLE -e ARCH_RV32I -d ARCH_RV64I \
  -d RISCV_ISA_C -d FPU -d SMP
make "${gnu_args[@]}" olddefconfig >"$ev/olddefconfig.log" 2>&1
for setting in CONFIG_NONPORTABLE CONFIG_ARCH_RV32I CONFIG_32BIT; do
  grep -Fxq "$setting=y" "$out/.config" || {
    echo "RV32I_LINUX=FAIL Kconfig_missing=$setting" >&2; exit 3;
  }
done
for setting in CONFIG_ARCH_RV64I CONFIG_RISCV_ISA_C CONFIG_FPU CONFIG_SMP; do
  grep -Fxq "# $setting is not set" "$out/.config" || {
    echo "RV32I_LINUX=FAIL Kconfig_wrong=$setting" >&2; exit 3;
  }
done
config_sha=$(sha256sum "$out/.config" | cut -d' ' -f1)
printf 'RV32I_LINUX_CONFIG=PASS original=6.6.143 arch=ARCH_RV32I no_C=1 no_FPU=1 no_SMP=1 sha256=%s\n' "$config_sha"
# The UNMODIFIED upstream kernel intentionally selects rv32ima for atomic /
# supervisor instructions. This is not the ISA used by MiniC's C emitter.
echo 'RV32I_LINUX_ISA_CONTRACT=PASS MiniC_C=rv32i original_Kbuild_asm=rv32ima separate_owners=true'
# First establish the original GNU build. A failure is a GNU/Kconfig/toolchain
# baseline blocker, NEVER reclassified as a MiniC defect.
if ! make "${gnu_args[@]}" -j4 Image >"$ev/gnu-image.log" 2>&1; then
  echo 'RV32I_LINUX_GNU_BASELINE=BLOCKED original_Kbuild_Image_failed' >&2
  tail -n 90 "$ev/gnu-image.log" >&2
  exit 4
fi
gnu_image="$out/arch/riscv/boot/Image"
test -s "$gnu_image"
test -s "$out/vmlinux"
riscv64-linux-gnu-readelf -h "$out/vmlinux" >"$ev/gnu-vmlinux-elf.txt"
grep -Eq 'Class:[[:space:]]+ELF32' "$ev/gnu-vmlinux-elf.txt"
grep -Eq 'Machine:[[:space:]]+RISC-V' "$ev/gnu-vmlinux-elf.txt"
gnu_sha=$(sha256sum "$gnu_image" | cut -d' ' -f1)
printf 'RV32I_LINUX_GNU_BASELINE=PASS ELF32=1 original_Kbuild=1 Image_SHA=%s\n' "$gnu_sha"
# Same real-Kbuild subset/receipt mechanism as the frozen RV64 2064.
# Select independent kernel language/ABI owner classes; no custom dummy C.
owners=(init/main.o kernel/fork.o mm/page_alloc.o lib/string.o arch/riscv/kernel/traps.o)
printf '%s\n' "${owners[@]}" >"$ev/owners.txt"
: >"$ev/minic-success.txt"
: >"$ev/minic-objects.sha256"
: >"$ev/kbuild-route.log"
for owner in "${owners[@]}"; do
  test -s "$out/$owner"
  rm -f "$out/$owner"
  MINIC="$MINIC_CC" REAL_CC=riscv64-linux-gnu-gcc \
    MINIC_KBUILD_RV32I_ORIGIN_AUDIT=1 \
    MINIC_KBUILD_SUCCESS_TRACE="$ev/minic-success.txt" \
    MINIC_KBUILD_OBJECT_HASH_TRACE="$ev/minic-objects.sha256" \
    MINIC_KBUILD_TRACE="$ev/kbuild-route.log" \
    MINIC_PRESERVE_FAILURE_INPUTS=1 \
    make "${gnu_args[@]}" \
      CC="$GITHUB_WORKSPACE/tests/external/linux/stage2_kbuild_cc.sh" \
      "$owner" >"$ev/$(echo "$owner" | tr / _).log" 2>&1 || {
        echo "RV32I_LINUX_MINIC=BLOCKED owner=$owner (original GNU Image remains proven)" >&2
        tail -n 100 "$ev/$(echo "$owner" | tr / _).log" >&2
        exit 5
      }
  test -s "$out/$owner"
  riscv64-linux-gnu-readelf -h "$out/$owner" |
    grep -Eq 'Class:[[:space:]]+ELF32'
  grep -Fxq "$owner" "$ev/minic-success.txt"
  grep -Fq "output=$owner" "$ev/kbuild-route.log"
done
python3 - "$ev" "$out" <<'PY'
from pathlib import Path
import hashlib
import sys
ev, out = map(Path, sys.argv[1:])
owners = (ev / "owners.txt").read_text().splitlines()
success = (ev / "minic-success.txt").read_text().splitlines()
assert len(owners) == 5 and success == owners, (owners, success)
hashes = (ev / "minic-objects.sha256").read_text().splitlines()
assert len(hashes) == 5, hashes
for owner, entry in zip(owners, hashes):
    expected, path = entry.split("  ", 1)
    assert path == owner, (owner, entry)
    assert hashlib.sha256((out / owner).read_bytes()).hexdigest() == expected
route = (ev / "kbuild-route.log").read_text()
assert route.count("minic rv32i_origin_audit=1 isa=rv32i abi=ilp32 ") == 5
assert route.count("assemble input=") == 5
print("RV32I_LINUX_MINIC=PASS original_Kbuild_owners=5/5 GNU_PP=1 MiniC_RV32I=1 GNU_AS=1 SHA=1 ELF32=1 no_C_fallback=1")
PY
# Reuse ORIGINAL GNU Kbuild link rather than creating an alternate linker.
if ! make "${gnu_args[@]}" -j4 Image >"$ev/minic-relink.log" 2>&1; then
  echo 'RV32I_LINUX_RELINK=BLOCKED genuine_Kbuild_5_owner_Image' >&2
  tail -n 90 "$ev/minic-relink.log" >&2
  exit 6
fi
test -s "$gnu_image"
printf 'RV32I_LINUX_RELINK=PASS original_Kbuild=1 candidate_owners=5 Image_SHA=%s QEMU_P1=NOT_RUN full_MiniC=NOT_RUN\n' "$(sha256sum "$gnu_image" | cut -d' ' -f1)"
