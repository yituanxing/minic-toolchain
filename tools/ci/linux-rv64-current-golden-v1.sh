#!/usr/bin/env bash
# Rebuild the *original GNU* RV64 Linux 6.6.143 golden only when Actions cache
# is missing on the current PR branch. Never accept a changed Kconfig or
# substitute one of the candidate's MiniC C objects into this reference.
set -Eeuo pipefail

if (( $# != 2 )); then
  echo 'usage: linux-rv64-current-golden-v1.sh <gnu-work> <pristine-linux-source>' >&2
  exit 64
fi
work=$1
source=$2
out="$work/out"
prov="$work/provenance"
expected_cfg=eeb04f304ebfb97a1079c187dc0c2ea3cc61fc50a55aa3cc776237c16ab19bbe
expected_source=dace1f8dc9c0dbf5df14f47e3229cd62c298e83049681731ef229f2ba7592932
test -s "$source/Makefile"
test ! -e "$out/.config"
mkdir -p "$out" "$prov"

export KBUILD_BUILD_USER=minic KBUILD_BUILD_HOST=minic-ci
export KBUILD_BUILD_TIMESTAMP='Thu Jan  1 00:00:00 UTC 1970'
make -C "$source" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- defconfig
"$source/scripts/config" --file "$out/.config" \
  -e BLK_DEV_INITRD -e DEVTMPFS -e DEVTMPFS_MOUNT \
  -e SERIAL_8250 -e SERIAL_8250_CONSOLE
make -C "$source" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- olddefconfig
actual_cfg=$(sha256sum "$out/.config" | cut -d' ' -f1)
if [[ "$actual_cfg" != "$expected_cfg" ]]; then
  echo "RV64_GNU_GOLDEN=FAIL Kconfig changed expected=$expected_cfg actual=$actual_cfg; refuse incompatible 2064-C owner set" >&2
  exit 4
fi
make -C "$source" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- -j4 Image
test "$(sha256sum "$out/.config" | cut -d' ' -f1)" = "$expected_cfg"
test -s "$out/vmlinux" && test -s "$out/arch/riscv/boot/Image"

# GNU image and its GNU-built initramfs must pass the original Python-era
# real QEMU contract *before* any GNU object can become the reference pool.
BUILD_DIR="$work/initramfs" \
  OUTPUT_INITRAMFS="$work/runtime-initramfs.cpio.gz" \
  RISCV_CC=riscv64-linux-gnu-gcc \
  bash tests/external/linux/build_runtime_initramfs.sh

LINUX_IMAGE="$out/arch/riscv/boot/Image" \
  INITRAMFS="$work/runtime-initramfs.cpio.gz" \
  BUILD_DIR="$work/gnu-qemu-p0" LINUX_RELEASE=6.6.143 \
  QEMU_TIMEOUT_SECONDS=90 LINUX_RUNTIME_PROFILE=full \
  bash tests/external/linux/runtime_boot.sh >"$prov/gnu-qemu-p0.log" 2>&1
grep -Fq 'LINUX_RUNTIME_BOOT lane=rdinit-init qemu_status=0 ' "$prov/gnu-qemu-p0.log"
grep -Fq 'LINUX_RUNTIME_BOOT lane=rdinit-sh qemu_status=0 ' "$prov/gnu-qemu-p0.log"
grep -Fq 'LINUX_RUNTIME_EXACT=PASS release=6.6.143' "$prov/gnu-qemu-p0.log"

LINUX_IMAGE="$out/arch/riscv/boot/Image" \
  INITRAMFS="$work/runtime-initramfs.cpio.gz" \
  BUILD_DIR="$work/gnu-qemu-p1" LINUX_RELEASE=6.6.143 \
  QEMU_TIMEOUT_SECONDS=90 LINUX_RUNTIME_PROFILE=p1 \
  bash tests/external/linux/runtime_boot.sh >"$prov/gnu-qemu-p1.log" 2>&1
grep -Fq 'LINUX_RUNTIME_BOOT lane=rdinit-init qemu_status=0 ' "$prov/gnu-qemu-p1.log"
grep -Fq 'LINUX_RUNTIME_EXACT=PASS release=6.6.143 profile=p1 lanes=1/1' "$prov/gnu-qemu-p1.log"
python3 tests/external/linux/runtime_v2_log_check.py \
  --profile p1 --log "$work/gnu-qemu-p1/rdinit-init.log" \
  >"$prov/gnu-qemu-p1-python.log" 2>&1

python3 - "$out" "$prov/gcc-objects.sha256" <<'PY_GNU_OBJECTS'
import hashlib
from pathlib import Path
import struct
import sys

out,manifest=map(Path,sys.argv[1:])
count=0
with manifest.open('w',encoding='utf-8') as dst:
    for obj in sorted(out.rglob('*.o')):
        with obj.open('rb') as src:
            hdr=src.read(20)
        if (len(hdr)<20 or hdr[:4]!=b'\x7fELF' or hdr[4]!=2 or hdr[5]!=1
                or struct.unpack_from('<H',hdr,16)[0]!=1
                or struct.unpack_from('<H',hdr,18)[0]!=243):
            continue
        digest=hashlib.sha256(obj.read_bytes()).hexdigest()
        dst.write(f'{digest}  {obj.relative_to(out).as_posix()}\n')
        count+=1
if count<2000:
    raise SystemExit('RV64_GNU_GOLDEN=FAIL implausibly small GNU native object pool')
print(f'RV64_GNU_OBJECT_POOL=PASS objects={count}')
PY_GNU_OBJECTS

python3 tools/ci/linux-runtime-gcc-full-c-objects-v1.py \
  --out "$out" --manifest "$prov/gcc-objects.sha256" \
  --output "$prov/original-c-owners.txt" \
  --excluded "$prov/original-non-c.tsv" --min-c-objects 2064
test "$(grep -Ec '^[^#[:space:]]+\.o$' "$prov/original-c-owners.txt")" = 2064

gcc_count=$(wc -l <"$prov/gcc-objects.sha256")
{
  echo schema=linux-gnu-exact-config-v1
  echo linux_release=6.6.143
  echo "linux_archive_sha256=$expected_source"
  echo "config_sha256=$actual_cfg"
  echo "gcc_version=$(riscv64-linux-gnu-gcc -dumpfullversion)"
  echo "gcc_target=$(riscv64-linux-gnu-gcc -dumpmachine)"
  echo "gcc_binary_sha256=$(sha256sum "$(command -v riscv64-linux-gnu-gcc)" | cut -d' ' -f1)"
  echo "gnu_ld_binary_sha256=$(sha256sum "$(command -v riscv64-linux-gnu-ld)" | cut -d' ' -f1)"
  echo "gnu_ar_binary_sha256=$(sha256sum "$(command -v riscv64-linux-gnu-ar)" | cut -d' ' -f1)"
  echo "gcc_object_count=$gcc_count"
  echo "vmlinux_sha256=$(sha256sum "$out/vmlinux" | cut -d' ' -f1)"
  echo "image_sha256=$(sha256sum "$out/arch/riscv/boot/Image" | cut -d' ' -f1)"
  echo qemu_contract=full_and_p1_passed
} >"$prov/gcc-baseline.txt"

echo "RV64_GNU_GOLDEN=PASS C=2064 original_GNU=1 source_sha256=$expected_source config_sha256=$actual_cfg QEMU=P0_P1_EXIT0"
