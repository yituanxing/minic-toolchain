#!/usr/bin/env bash
set -Eeuo pipefail

# Verify the *already linked* immutable Linux 6.6.143 Image from the seven
# certified MiniC shard run. No MiniC rebuild, GNU link or object substitution.
# Artifact download happens in the existing gcc-shard-verify job.
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
archive="${1:?usage: linux-runtime-large-image-qemu-v0.sh artifact-extraction-dir}"
ev="$repo/build/linux-runtime-large-image-qemu-v0"
mkdir -p "$ev"
# The historical 145/203-PASS MiniC Image and the current all-2064
# MiniC Image must not be mixed or silently substituted. Pin source run,
# kernel bytes, and the reference historical file size for a causal A/B.
historical=${LARGE_IMAGE_HISTORICAL_NINE_P1:-0}
if [[ "$historical" == 1 ]]; then
  expected_sha=8e2a6b39a2b90107dd1c4093bea88d229806812ffb344cea03d2dd855910e0f5
  expected_bytes=215502848
  source_run=38135950731
  source_commit=b4ca2dd65a3213c6c5e430c678d406392033ac1e
  image=
  # The historical nine-MiniC-owner bundle has a different layout from
  # sharded-gnu-minic-QEMU. Accept ONLY the byte-identical known-good Image.
  while IFS= read -r -d '' candidate; do
    if [[ $(sha256sum "$candidate" | cut -d' ' -f1) == "$expected_sha" ]]; then
      image="$candidate"
      break
    fi
  done < <(find "$archive" -type f -name Image -print0)
else
  expected_sha=dba3295a111b861be13dade671184918635662a82df8ad7997deb2da5c7d147f
  expected_bytes=215511040
  source_run=38204602644
  source_commit=d5dde03594665842f2bb3b8556f842f511a5dfa4
  image=$(find "$archive" -type f -path '*/trials/full_all/Image' -print -quit)
fi
if [[ -z "$image" || ! -s "$image" ]]; then
  echo "LARGE_IMAGE_REUSE=FAIL reason=certified_image_missing artifact=$archive historical=$historical" >&2
  find "$archive" -type f -printf '%P (%s bytes)\n' | head -n 65 || true
  exit 2
fi
actual_sha=$(sha256sum "$image" | cut -d' ' -f1)
if [[ "$actual_sha" != "$expected_sha" ]]; then
  echo "LARGE_IMAGE_REUSE=FAIL reason=image_digest_mismatch expected=$expected_sha actual=$actual_sha" >&2
  exit 2
fi
image_bytes=$(stat -c '%s' "$image")
[[ "$image_bytes" == "$expected_bytes" ]] || {
  echo "LARGE_IMAGE_REUSE=FAIL reason=image_size expected=$expected_bytes observed=$image_bytes" >&2
  exit 2
}
printf '%s\n' "source_run=$source_run" "source_commit=$source_commit" \
  "image_sha256=$actual_sha" "image_bytes=$image_bytes" "compiler=MiniC" \
  "assembler=GNU-AS" "linker=GNU-LD" "qemu_memory=2G" \
  "original_145_203_pass_historical=$historical" \
  >"$ev/provenance.txt"
echo "LARGE_IMAGE_REUSE=PASS sha256=$actual_sha bytes=$image_bytes source_run=$source_run historical=$historical"

BUILD_DIR="$ev/initramfs" OUTPUT_INITRAMFS="$ev/runtime-initramfs.cpio.gz" \
  RISCV_CC=riscv64-linux-gnu-gcc \
  bash "$repo/tests/external/linux/build_runtime_initramfs.sh" \
  >"$ev/initramfs.log" 2>&1
test -s "$ev/runtime-initramfs.cpio.gz"
echo "LARGE_IMAGE_INITRAMFS=PASS gcc_generated=true"

# QEMU 8.2 places -initrd at kernel_entry+128MiB regardless of -m.
# Generate the identical QEMU virt DTB and move only the ramdisk to a
# separate physical RAM page; the guest still receives real /chosen
# linux,initrd-start/end, the original Image and original initramfs bytes.
# This is a QEMU loader workaround, not a kernel/Minic code transformation.
command -v fdtput >/dev/null || {
  echo "LARGE_IMAGE_DTB=FAIL reason=fdtput_missing" >&2
  exit 2
}
dtb="$ev/virt-large-image.dtb"
qemu-system-riscv64 -M "virt,dumpdtb=$dtb" -cpu max \
  -m 2G -smp 1 -nographic -no-reboot -bios default \
  -kernel "$image" >"$ev/virt-dumpdtb.log" 2>&1
test -s "$dtb"
fdtget -p "$dtb" /chosen >/dev/null 2>&1 || fdtput -c "$dtb" /chosen
load_addr=0xa0000000
initrd_bytes=$(stat -c '%s' "$ev/runtime-initramfs.cpio.gz")
load_end=$((0xa0000000+initrd_bytes))
image_end=$((0x80200000+image_bytes))
((0xa0000000 >= image_end && load_end < 0x100000000)) || {
  echo "LARGE_IMAGE_DTB=FAIL reason=memory_layout_overlap" >&2
  exit 2
}
printf -v initrd_end_hex '%x' "$load_end"
fdtput -t x "$dtb" /chosen linux,initrd-start 0 a0000000
fdtput -t x "$dtb" /chosen linux,initrd-end 0 "$initrd_end_hex"
start_cells=$(fdtget -t x "$dtb" /chosen linux,initrd-start)
end_cells=$(fdtget -t x "$dtb" /chosen linux,initrd-end)
[[ "$start_cells" == "0 a0000000" && "$end_cells" == "0 $initrd_end_hex" ]] || {
  echo "LARGE_IMAGE_DTB=FAIL reason=dtb_property_mismatch start=$start_cells end=$end_cells" >&2
  exit 2
}
echo "LARGE_IMAGE_DTB=PASS image_end=$(printf '%x' "$image_end") initrd_start=a0000000 initrd_end=$initrd_end_hex qemu_8_2_loader_workaround=1"
export LINUX_RUNTIME_CUSTOM_DTB="$dtb"
export LINUX_RUNTIME_INITRD_LOAD_ADDR="$load_addr"

# The original 512M QEMU guest has a ROM-loading conflict with a 215MB
# MiniC Image plus initramfs. Change only the *host* QEMU memory map, not
# the Linux image, initramfs, source config, flags, or runtime oracles.
# Leave the reference QEMU runner's default 512M entirely unchanged.
run_profile() {
  local profile="$1" timeout="$2" label="${3:-$1}" rc=0
  echo "LARGE_IMAGE_QEMU=START profile=$profile sample=$label guest_memory=2G timeout=$timeout"
  if LINUX_IMAGE="$image" INITRAMFS="$ev/runtime-initramfs.cpio.gz" \
     BUILD_DIR="$ev/qemu-$label" LINUX_RELEASE=6.6.143 \
     LINUX_RUNTIME_QEMU_MEMORY=2G \
     LINUX_RUNTIME_PROFILE="$profile" QEMU_TIMEOUT_SECONDS="$timeout" \
     QEMU_SHELL_INPUT_DELAY_SECONDS=22 \
     bash "$repo/tests/external/linux/runtime_boot.sh" \
       >"$ev/$label.stdout.log" 2>&1; then
    rc=0
  else
    rc=$?
  fi
  echo "LARGE_IMAGE_QEMU_PROFILE profile=$profile label=$label rc=$rc"
  grep -aE 'Linux version|Kernel command line|devtmpfs:|Run /init|Run /bin/sh|INIT_|USER_SHELL_OK|DONE_COMMANDS|RUNTIME_V2|Kernel panic|Oops:|BUG:|Unable to handle|ROM regions are overlapping' \
    "$ev/qemu-$label"/*.log 2>/dev/null | tail -n 65 || true
  if ((rc != 0)); then
    echo "LARGE_IMAGE_QEMU=FAIL profile=$profile label=$label rc=$rc"
    tail -n 85 "$ev/$label.stdout.log" || true
    return "$rc"
  fi
  echo "LARGE_IMAGE_QEMU=PASS profile=$profile label=$label guest_memory=2G image_unchanged=true"
}
# Shell is independent of the IPv6 P1 syscall path and must be proved
# before a genuine runtime panic stops the failing P1 lane.
if [[ "$historical" == 1 ]]; then
  # Historical MiniC 145/203-PASS image, not an all-current 2064 compile.
  # Test exactly the modern P1 with three fresh QEMU processes and SAME Image.
  # Failures are real negative evidence; preserve all raw logs, do not abort
  # before collecting the repeated guest outcome.
  run_profile fast 75
  printf 'attempt\timage_sha256\tp1_verdict\tqemu_rc\n' >"$ev/historical-p1-verdicts.tsv"
  passes=0
  fails=0
  for attempt in 1 2 3; do
    result=FAIL
    rc=0
    run_profile p1 95 "p1-historical-$attempt" || rc=$?
    if ((rc==0)); then
      result=PASS
      ((++passes))
    else
      ((++fails))
    fi
    [[ $(sha256sum "$image" | cut -d' ' -f1) == "$expected_sha" ]] || {
      echo "HISTORICAL_NINE_P1=ERROR source_image_mutated"; exit 8;
    }
    printf '%s\t%s\t%s\t%s\n' "$attempt" "$expected_sha" "$result" "$rc" >>"$ev/historical-p1-verdicts.tsv"
    echo "HISTORICAL_NINE_P1_ATTEMPT=$attempt verdict=$result original_145_203_pass_image=true"
  done
  if ((passes==3)); then
    verdict=PASS
  elif ((fails==3)); then
    verdict=FAIL
  else
    verdict=VARIABLE
  fi
  echo "HISTORICAL_NINE_P1=RESULT verdict=$verdict passes=$passes fails=$fails boots=3 original_image_sha=$expected_sha image_unchanged=true"
  exit 0
fi
run_profile fast 75
run_profile shell 90
run_profile p1 90
test "$(sha256sum "$image" | cut -d' ' -f1)" = "$expected_sha"
echo "LARGE_IMAGE_RUNTIME=PASS fast=1 p1=12_syscalls shell=1 same_original_image=true"
