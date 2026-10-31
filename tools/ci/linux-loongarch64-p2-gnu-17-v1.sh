#!/usr/bin/env bash
# Linux 6.6.143 LoongArch64: real GNU QEMU P2 17/18, KVM STRUCTURAL_BLOCKED.
# This must NEVER emit or be interpreted as a full P2 PASS.
set -Eeuo pipefail

if (( $# != 3 )); then
  echo 'usage: linux-loongarch64-p2-gnu-17-v1.sh <pristine-linux-6.6.143-source> <sha-pinned-QEMU_EFI.fd> <fresh-work-dir>' >&2
  exit 64
fi
src=$(realpath "$1")
firmware=$(realpath "$2")
work=$(realpath -m "$3")
repo=$(cd "$(dirname "$0")/../.." && pwd)
guest="$repo/tests/external/linux/runtime_initramfs"
checker="$repo/tools/ci/linux-loongarch64-p2-gnu-17-check-v1.py"
cc=loongarch64-linux-gnu-gcc-13
cross=loongarch64-linux-gnu-
firmware_sha=b375639ebcf7a873d2cab67f0c9c1c6c208fc3f1bcba4083172fa20c4fb7e1ab
probe_sha=093ecf2b651084fa33250b5587f04cf87c118f0e35cf2c01aea3b76f2e6fc4a4

[[ ! -e "$work" && -f "$src/Makefile" && -x "$src/scripts/config" && -s "$firmware" ]] || {
  echo 'LA64_P2_17=FAIL reason=existing-output-or-missing-source-firmware' >&2; exit 2;
}
[[ ! -e "$src/arch/loongarch/kvm" ]] || {
  echo 'LA64_P2_17=FAIL reason=kvm-arch-implementation-present' >&2; exit 2;
}
[[ "$(make -s -C "$src" kernelversion)" == 6.6.143 ]] || {
  echo 'LA64_P2_17=FAIL reason=kernel-version-not-6.6.143' >&2; exit 2;
}
printf '%s  %s\n' "$firmware_sha" "$firmware" | sha256sum -c -
printf '%s  %s\n' "$probe_sha" "$guest/p2_probe.c" | sha256sum -c -
python3 "$checker" --source-guard-only
for tool in "$cc" qemu-system-loongarch64 cpio gzip mkfs.ext4 timeout python3; do
  command -v "$tool" >/dev/null || {
    echo "LA64_P2_17=FAIL reason=missing-tool tool=$tool" >&2; exit 2;
  }
done

mkdir -p "$work"/{build,root/bin,root/dev,root/proc,root/sys,root/tmp,fixture,evidence}
out="$work/build"
root="$work/root"
evidence="$work/evidence"
rootfs="$work/fixture/ext4.baseline.raw"
nvme="$work/fixture/nvme.baseline.raw"
initrd="$work/fixture/initramfs.cpio.gz"

# Every feasible P2 Kconfig feature must be builtin in this module-free initrd.
# Exactly one canonical feature is impossible: arch/loongarch/kvm is absent.
required=(
  IPV6 OVERLAY_FS BTRFS_FS BLK_DEV_DM EXT4_FS VIRTIO_BLK VIRTIO_PCI
  VIRTIO_MMIO NETFILTER NF_CONNTRACK IP_NF_IPTABLES IP6_NF_IPTABLES
  IP_NF_FILTER IP6_NF_FILTER BRIDGE BRIDGE_NETFILTER VETH
  CRYPTO_USER_API_HASH CRYPTO_SHA256 CRYPTO_SHA512 CRYPTO_SHA3
  CRYPTO_BLAKE2B DRM_VIRTIO_GPU BLK_DEV_NVME NET_NS NAMESPACES
)
export KBUILD_BUILD_USER=minic KBUILD_BUILD_HOST=minic-ci
export KBUILD_BUILD_TIMESTAMP='Thu Jan  1 00:00:00 UTC 1970'
make -s -C "$src" O="$out" ARCH=loongarch CROSS_COMPILE="$cross" CC="$cc" \
  loongson3_defconfig >"$evidence/kconfig.log" 2>&1
args=()
for symbol in "${required[@]}"; do args+=(--enable "$symbol"); done
"$src/scripts/config" --file "$out/.config" "${args[@]}"
make -s -C "$src" O="$out" ARCH=loongarch CROSS_COMPILE="$cross" CC="$cc" \
  olddefconfig >>"$evidence/kconfig.log" 2>&1
printf 'symbol\trequested\teffective\n' >"$evidence/kconfig.tsv"
for symbol in "${required[@]}"; do
  if ! grep -Fxq "CONFIG_${symbol}=y" "$out/.config"; then
    echo "LA64_P2_17=FAIL reason=unresolved-config symbol=$symbol" >&2
    exit 1
  fi
  printf '%s\ty\ty\n' "$symbol" >>"$evidence/kconfig.tsv"
done
if grep -Eq '^CONFIG_KVM(_[A-Z0-9_]+)?=[ym]$' "$out/.config"; then
  echo 'LA64_P2_17=FAIL reason=unexpected-kvm-config' >&2; exit 1
fi
printf 'KVM\ty\tSTRUCTURAL_BLOCKED-linux-6.6.143-loongarch-no-kvm\n' >>"$evidence/kconfig.tsv"
sha256sum "$out/.config" "$guest/p2_probe.c" "$firmware" >"$evidence/config-input.sha256"

# Build the original upstream GNU kernel, not MiniC or GNU C-owner fallback.
# LoongArch QEMU boots the EFI image, not the ELF vmlinux build by-product.
make -C "$src" O="$out" ARCH=loongarch CROSS_COMPILE="$cross" CC="$cc" \
  -j "${LA64_P2_BUILD_JOBS:-4}" vmlinuz.efi >"$evidence/gnu-kbuild.log" 2>&1 || {
    tail -n 100 "$evidence/gnu-kbuild.log" >&2
    echo 'LA64_P2_17=FAIL reason=gnu-kbuild' >&2
    exit 1
  }
image="$out/arch/loongarch/boot/vmlinuz.efi"
[[ -s "$image" ]] || {
  echo 'LA64_P2_17=FAIL reason=missing-real-efi-kernel' >&2; exit 1;
}

# Canonical original P0/P1 probes plus a Loong-specific 17-call main that
# includes the canonical P2 C implementation (no cloned fake check bodies).
"$cc" -static -O2 -Wall -Wextra -Werror "$guest/loongarch64_p2_17_init.c" -o "$root/init"
"$cc" -static -O2 -Wall -Wextra -Werror "$guest/probe.c" -o "$root/bin/runtime-probe"
"$cc" -static -O2 -Wall -Wextra -Werror "$guest/loongarch64_p2_17_probe.c" -o "$root/bin/p2-probe"
file "$root/init" "$root/bin/runtime-probe" "$root/bin/p2-probe" >"$evidence/guest-elf.txt"
(
  cd "$root"
  find . -print0 | LC_ALL=C sort -z | cpio --null -o --format=newc --quiet | gzip -9
) >"$initrd"

# Sparse raw disks are each an actual QEMU PCI block device; snapshot=on in
# QEMU preserves them. No RV64 ELF or module is reused across ISAs.
truncate -s 128M "$rootfs"
mkfs.ext4 -F -q "$rootfs"
truncate -s 64M "$nvme"
sha256sum "$image" "$initrd" "$rootfs" "$nvme" "$firmware" >"$evidence/qemu-input.sha256"

command=(
  qemu-system-loongarch64 -machine virt -cpu la464
  -m 2G -smp 1 -nographic -no-reboot -nic none -vga none
  -bios "$firmware" -kernel "$image" -initrd "$initrd"
  -append 'console=ttyS0 loglevel=8 panic=-1 rdinit=/init minic_runtime=p2 nf_conntrack.enable_hooks=1'
  -drive "if=none,id=p2root,file=$rootfs,format=raw,snapshot=on"
  -device virtio-blk-pci,drive=p2root
  -device virtio-gpu-pci
  -drive "if=none,id=p2nvmedisk,file=$nvme,format=raw,snapshot=on"
  -device nvme,id=p2nvme,serial=MINICP2
  -device nvme-ns,drive=p2nvmedisk,bus=p2nvme
)
printf '%q ' "${command[@]}" >"$evidence/qemu-command.txt"
printf '\n' >>"$evidence/qemu-command.txt"
set +e
timeout --signal=TERM "${LA64_P2_QEMU_TIMEOUT_SECONDS:-900}" \
  "${command[@]}" </dev/null >"$evidence/qemu.log" 2>&1
qemu_rc=$?
set -e
printf 'qemu_rc=%d\n' "$qemu_rc" >"$evidence/result.txt"
set +e
python3 "$checker" --source "$src" --config "$out/.config" \
  --qemu-rc "$qemu_rc" --log "$evidence/qemu.log" \
  >"$evidence/oracle.txt" 2>&1
oracle_rc=$?
set -e
printf 'oracle_rc=%d\n' "$oracle_rc" >>"$evidence/result.txt"
sha256sum "$evidence/qemu.log" >>"$evidence/result.txt"
if (( oracle_rc != 0 )); then
  tail -n 50 "$evidence/oracle.txt" >&2
  echo "LA64_P2_17=FAIL qemu_rc=$qemu_rc oracle_rc=$oracle_rc evidence=$evidence" >&2
  exit 1
fi
echo "LA64_P2_17=STRUCTURAL_BLOCKED actual_p2=17/18 kvm=unimplemented_linux_6.6.143 qemu_rc=0 evidence=$evidence"
