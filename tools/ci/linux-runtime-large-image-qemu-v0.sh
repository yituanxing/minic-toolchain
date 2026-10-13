#!/usr/bin/env bash
set -Eeuo pipefail

# Verify an ALREADY linked SHA-pinned Linux 6.6.143 Image with P0/shell/P1.
# Historical defaults are immutable; opt-in accepted-P1 replay sets explicit
# source-run, object-selection and Image digest from its certified artifact.
# No MiniC rebuild, link or object substitution. Same gcc-shard-verify job.
repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
archive="${1:?usage: linux-runtime-large-image-qemu-v0.sh artifact-extraction-dir}"
ev="$repo/build/linux-runtime-large-image-qemu-v0"
mkdir -p "$ev"
image_pattern=${LINUX_LARGE_IMAGE_SUBPATH:-'*/trials/full_all/Image'}
image=$(find "$archive" -type f -path "$image_pattern" -print -quit)
if [[ -z "$image" || ! -s "$image" ]]; then
  echo "LARGE_IMAGE_REUSE=FAIL reason=original_image_missing artifact=$archive" >&2
  exit 2
fi
expected_sha=${LINUX_LARGE_IMAGE_SHA:-dba3295a111b861be13dade671184918635662a82df8ad7997deb2da5c7d147f}
source_run=${LINUX_LARGE_IMAGE_SOURCE_RUN:-38204602644}
source_commit=${LINUX_LARGE_IMAGE_SOURCE_COMMIT:-d5dde03594665842f2bb3b8556f842f511a5dfa4}
if [[ ! "$expected_sha" =~ ^[0-9a-f]{64}$ || ! "$source_run" =~ ^[0-9]+$ ||
      ! "$source_commit" =~ ^[0-9a-f]{40}$ ]]; then
  echo "LARGE_IMAGE_REUSE=FAIL malformed_expected_provenance" >&2
  exit 2
fi
actual_sha=$(sha256sum "$image" | cut -d' ' -f1)
if [[ "$actual_sha" != "$expected_sha" ]]; then
  echo "LARGE_IMAGE_REUSE=FAIL reason=image_digest_mismatch expected=$expected_sha actual=$actual_sha" >&2
  exit 2
fi
image_bytes=$(stat -c '%s' "$image")
[[ "$image_bytes" == 215511040 ]] || {
  echo "LARGE_IMAGE_REUSE=FAIL reason=image_size bytes=$image_bytes" >&2
  exit 2
}
printf '%s\n' "source_run=$source_run" "source_commit=$source_commit" \ 
  "image_sha256=$actual_sha" "image_bytes=$image_bytes" "compiler=MiniC" \
  "assembler=GNU-AS" "linker=GNU-LD" "qemu_memory=2G" \
  >"$ev/provenance.txt"
echo "LARGE_IMAGE_REUSE=PASS sha256=$actual_sha bytes=$image_bytes source_run=$source_run image_pattern=$image_pattern"
# Before re-running P0/shell/P1, audit the exact first 24-boot artifact.
# A bare linked Image with the right digest is not enough to claim that the
# source had 2064 MiniC C objects, eight opt-in producers or 24 true P1 PASSes.
if [[ "${LINUX_LARGE_IMAGE_VERIFY_ACCEPTED_P1:-0}" == 1 ]]; then
  python3 - "$archive" "$expected_sha" <<'PY_ACCEPTED'
from pathlib import Path
import csv
import sys
root, sha = Path(sys.argv[1]), sys.argv[2]
p1 = root / "p1-defined-crossblock"
with (p1 / "p1-repeats.tsv").open(newline="") as f:
    repeats = list(csv.DictReader(f, delimiter="\t"))
assert len(repeats) == 24, "not 24 P1 repeats"
assert all(row["trial"] == str(index) and
           row["image_sha256"] == sha and row["verdict"] == "PASS"
           for index, row in enumerate(repeats, 1)), "mixed-image or non-PASS P1 history"
with (p1 / "object-diff.tsv").open(newline="") as f:
    diffs = list(csv.DictReader(f, delimiter="\t"))
assert len(diffs) == 8 and all(row["different"] == "1" for row in diffs), (
    "eight changed MiniC owners not demonstrated"
)
with (p1 / "selected.txt").open() as f:
    selected = [s.strip() for s in f if s.strip()]
assert len(selected) == 8 and set(selected) == {row["owner"] for row in diffs}
assert len((root / "minic-manifest.sha256").read_text().splitlines()) == 2064
assert ("MINIC_ROUTE_AUDIT=PASS minic_compiled=8 "
        "ancillary_minic_compiled=0 authenticated_rebuilds=0" in
        (p1 / "route-audit.log").read_text()), "MiniC provenance missing"
for i in range(1, 25):
    serial = (root / f"trials/p1_defined_crossblock_{i}/qemu/rdinit-init.log"
              ).read_text(errors="replace")
    assert "RUNTIME_V2_SYSCALL_END pass=12 fail=0" in serial
    assert "RUNTIME_V2_SYSCALL_PASS ipv6-tcp-loopback" in serial
    assert "MINIC_LINUX_RUNTIME_INIT_END fail=0" in serial
    assert "Insufficient stack space to handle exception!" not in serial
    assert "Kernel panic" not in serial
print("P1_ACCEPTED_ARTIFACT=PASS same_image=24/24 p1_syscalls=12/12 "
      "ipv6=24/24 minic_c=2064 changed_minic_c=8 gcc_c=0 "
      f"sha256={sha} extra_profile_certification=NOT_RUN")
PY_ACCEPTED
fi

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
  local profile="$1" timeout="$2" rc=0
  echo "LARGE_IMAGE_QEMU=START profile=$profile guest_memory=2G timeout=$timeout"
  if LINUX_IMAGE="$image" INITRAMFS="$ev/runtime-initramfs.cpio.gz" \
     BUILD_DIR="$ev/qemu-$profile" LINUX_RELEASE=6.6.143 \
     LINUX_RUNTIME_QEMU_MEMORY=2G \
     LINUX_RUNTIME_PROFILE="$profile" QEMU_TIMEOUT_SECONDS="$timeout" \
     QEMU_SHELL_INPUT_DELAY_SECONDS=22 \
     bash "$repo/tests/external/linux/runtime_boot.sh" \
       >"$ev/$profile.stdout.log" 2>&1; then
    rc=0
  else
    rc=$?
  fi
  echo "LARGE_IMAGE_QEMU_PROFILE profile=$profile rc=$rc"
  grep -aE 'Linux version|Kernel command line|devtmpfs:|Run /init|Run /bin/sh|INIT_|USER_SHELL_OK|DONE_COMMANDS|RUNTIME_V2|Kernel panic|Oops:|BUG:|Unable to handle|ROM regions are overlapping' \
    "$ev/qemu-$profile"/*.log 2>/dev/null | tail -n 65 || true
  if ((rc != 0)); then
    echo "LARGE_IMAGE_QEMU=FAIL profile=$profile rc=$rc"
    tail -n 85 "$ev/$profile.stdout.log" || true
    return "$rc"
  fi
  echo "LARGE_IMAGE_QEMU=PASS profile=$profile guest_memory=2G image_unchanged=true"
}
# Shell is independent of the IPv6 P1 syscall path and must be proved
# before a genuine runtime panic stops the failing P1 lane.
run_profile fast 75
run_profile shell 90
run_profile p1 90
test "$(sha256sum "$image" | cut -d' ' -f1)" = "$expected_sha"
echo "LARGE_IMAGE_RUNTIME=PASS fast=1 p1=12_syscalls shell=1 same_original_image=true source_run=$source_run sha256=$actual_sha"
