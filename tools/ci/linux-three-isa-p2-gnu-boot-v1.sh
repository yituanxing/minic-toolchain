#!/usr/bin/env bash
# Execute actual GNU Linux 6.6.143 P2 guest on one of the three target ISAs.
# No marker, QEMU 0-exit or feature SKIP is independently a full P2 success.
set -Eeuo pipefail

if (( $# < 6 || $# > 7 )); then
  echo 'usage: linux-three-isa-p2-gnu-boot-v1.sh <target> <GNU-image> <initrd.cpio.gz> <ext4.img> <nvme.img> <fresh-evidence-dir> [LoongArch-QEMU_EFI.fd]' >&2
  exit 64
fi

target=$1
image=$(realpath "$2")
initrd=$(realpath "$3")
rootfs=$(realpath "$4")
nvme=$(realpath "$5")
evidence=$(realpath -m "$6")
firmware=${7:-}
repo=$(cd "$(dirname "$0")/../.." && pwd)

case "$target" in
  x86_64)
    qemu=${QEMU_SYSTEM:-qemu-system-x86_64}
    console=ttyS0
    machine_args=(-machine pc -cpu max)
    ;;
  aarch64)
    qemu=${QEMU_SYSTEM:-qemu-system-aarch64}
    console=ttyAMA0
    # P2 requires a usable /dev/kvm API in the *guest*. QEMU's ARM virt
    # machine does not expose EL2 unless virtualization=on is requested.
    machine_args=(-machine virt,virtualization=on -cpu cortex-a57)
    ;;
  loongarch64)
    qemu=${QEMU_SYSTEM:-qemu-system-loongarch64}
    console=ttyS0
    machine_args=(-machine virt -cpu la464)
    [[ -s "$firmware" ]] || {
      echo 'P2_GNU_BOOT=FAIL reason=loongarch-firmware-required' >&2
      exit 2
    }
    ;;
  *) echo "P2_GNU_BOOT=FAIL reason=unknown-target target=$target" >&2; exit 64 ;;
esac
for file in "$image" "$initrd" "$rootfs" "$nvme"; do
  [[ -s "$file" ]] || { echo "P2_GNU_BOOT=FAIL reason=missing-input path=$file" >&2; exit 2; }
done
[[ ! -e "$evidence" ]] || {
  echo "P2_GNU_BOOT=FAIL reason=output-already-exists path=$evidence" >&2
  exit 2
}

qemu_path=$(command -v "$qemu") || {
  echo "P2_GNU_BOOT=FAIL reason=missing-qemu binary=$qemu" >&2; exit 2;
}
mkdir -p "$evidence"
sha256sum "$image" "$initrd" "$rootfs" "$nvme" >"$evidence/input.sha256"
command=(
  "$qemu_path" "${machine_args[@]}"
  -m 1024M -smp 1 -nographic -no-reboot -nic none -vga none
  -kernel "$image" -initrd "$initrd"
  -append "console=$console loglevel=8 panic=-1 rdinit=/init minic_runtime=p2 nf_conntrack.enable_hooks=1"
  -drive "if=none,id=p2root,file=$rootfs,format=raw,snapshot=on"
  -device virtio-blk-pci,drive=p2root
  -device virtio-gpu-pci
  -drive "if=none,id=p2nvmedisk,file=$nvme,format=raw,snapshot=on"
  -device nvme,id=p2nvme,serial=MINICP2
  -device nvme-ns,drive=p2nvmedisk,bus=p2nvme
)
if [[ "$target" == loongarch64 ]]; then
  command+=(-bios "$(realpath "$firmware")")
  sha256sum "$firmware" >>"$evidence/input.sha256"
elif [[ -n "${QEMU_BIOS:-}" ]]; then
  # Optional explicit SeaBIOS when using a non-installed QEMU binary.
  [[ "$target" == x86_64 && -s "$QEMU_BIOS" ]] || {
    echo 'P2_GNU_BOOT=FAIL reason=unexpected-bios' >&2; exit 2;
  }
  command+=(-bios "$(realpath "$QEMU_BIOS")")
  sha256sum "$QEMU_BIOS" >>"$evidence/input.sha256"
fi

printf '%q ' "${command[@]}" >"$evidence/qemu-command.txt"
printf '\n' >>"$evidence/qemu-command.txt"
timeout=${QEMU_TIMEOUT_SECONDS:-180}
set +e
TMPDIR="$evidence" timeout --signal=TERM "$timeout" "${command[@]}" \
  </dev/null >"$evidence/qemu.log" 2>&1
rc=$?
set -e
printf 'qemu_rc=%s\n' "$rc" >"$evidence/result.txt"
sha256sum "$evidence/qemu.log" >>"$evidence/result.txt"

set +e
python3 "$repo/tests/external/linux/runtime_v2_log_check.py" \
  --profile p2 --qemu-rc "$rc" --log "$evidence/qemu.log" \
  >"$evidence/oracle.txt" 2>&1
oracle_rc=$?
set -e
printf 'oracle_rc=%s\n' "$oracle_rc" >>"$evidence/result.txt"
if (( oracle_rc != 0 )); then
  echo "P2_GNU_BOOT=FAIL target=$target qemu_rc=$rc oracle_rc=$oracle_rc evidence=$evidence"
  exit 1
fi
echo "P2_GNU_BOOT=PASS target=$target scope=18-distinct-canonical-checks qemu_rc=$rc evidence=$evidence"
