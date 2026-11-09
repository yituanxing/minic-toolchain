#!/usr/bin/env bash
# Resolve, and fail closed on, the *GNU* P2 kernel configuration prerequisites.
# This is a feature/Kconfig audit only, NEVER a MiniC or QEMU runtime PASS.
set -Eeuo pipefail

if (( $# != 3 )); then
  echo 'usage: linux-three-isa-p2-kconfig-v1.sh <x86_64|aarch64|loongarch64> <linux-6.6.143-tree> <fresh-out-dir>' >&2
  exit 64
fi

target=$1
src=$(realpath "$2")
out=$(realpath -m "$3")
case "$target" in
  x86_64)
    arch=x86; cross=; cc=gcc; defconfig=x86_64_defconfig ;;
  aarch64)
    arch=arm64; cross=aarch64-linux-gnu-; cc=aarch64-linux-gnu-gcc; defconfig=defconfig ;;
  loongarch64)
    arch=loongarch; cross=loongarch64-linux-gnu-; cc=loongarch64-linux-gnu-gcc-13; defconfig=loongson3_defconfig ;;
  *) echo "P2_KCONFIG=FAIL reason=unknown-target target=$target" >&2; exit 64 ;;
esac

[[ -f "$src/Makefile" && -x "$src/scripts/config" ]] || {
  echo "P2_KCONFIG=FAIL reason=missing-source path=$src" >&2; exit 2;
}
release=$(make -s -C "$src" kernelversion)
[[ "$release" == 6.6.143 ]] || {
  echo "P2_KCONFIG=FAIL reason=wrong-kernel-release expected=6.6.143 actual=$release" >&2
  exit 2
}
[[ ! -e "$out/.config" ]] || {
  echo "P2_KCONFIG=FAIL reason=existing-config path=$out/.config" >&2; exit 2;
}
command -v "$cc" >/dev/null || {
  echo "P2_KCONFIG=FAIL reason=missing-toolchain cc=$cc" >&2; exit 2;
}

# Features with real runtime assertions in runtime_v2_checks.tsv. KVM is
# intentionally NOT silently omitted on LoongArch: Linux 6.6.143 lacks its
# arch/loongarch/kvm implementation, and the audit must report the block.
required=(
  IPV6 OVERLAY_FS BTRFS_FS BLK_DEV_DM EXT4_FS
  VIRTIO_BLK VIRTIO_PCI VIRTIO_MMIO NETFILTER NF_CONNTRACK
  IP_NF_IPTABLES IP6_NF_IPTABLES IP_NF_FILTER IP6_NF_FILTER
  BRIDGE BRIDGE_NETFILTER VETH CRYPTO_USER_API_HASH
  CRYPTO_SHA256 CRYPTO_SHA512 CRYPTO_SHA3 CRYPTO_BLAKE2B
  KVM DRM_VIRTIO_GPU BLK_DEV_NVME
)
prerequisites=(
  BLOCK PCI PCI_MSI NET INET NETDEVICES NET_CORE MD DRM
  VIRTIO_MENU CRYPTO_USER_API NETFILTER_ADVANCED NETFILTER_XTABLES VIRTUALIZATION
  NAMESPACES NET_NS
)

mkdir -p "$out"
make -s -C "$src" O="$out" ARCH="$arch" CROSS_COMPILE="$cross" CC="$cc" \
  "$defconfig"
args=()
for symbol in "${required[@]}" "${prerequisites[@]}"; do
  args+=(--enable "$symbol")
done
# On x86, CONFIG_KVM is the core alone; a vendor backend is necessary for
# registering the misc device. The TCG guest may still lack VMX instructions,
# which must be adjudicated by the actual guest check, never by this config.
if [[ "$target" == x86_64 ]]; then
  args+=(--enable KVM_INTEL)
fi
"$src/scripts/config" --file "$out/.config" "${args[@]}"
make -s -C "$src" O="$out" ARCH="$arch" CROSS_COMPILE="$cross" CC="$cc" \
  olddefconfig

receipt="$out/p2-kconfig-status.tsv"
printf '%s\n' $'symbol\trequested\teffective' >"$receipt"
ready=0
missing=()
for symbol in "${required[@]}"; do
  value=disabled
  if grep -Fxq "CONFIG_${symbol}=y" "$out/.config"; then
    value=y
    ((ready+=1))
  elif grep -Fxq "CONFIG_${symbol}=m" "$out/.config"; then
    value=m
  fi
  printf '%s\ty\t%s\n' "$symbol" "$value" >>"$receipt"
  if [[ "$value" != y ]]; then
    missing+=("$symbol=$value")
  fi
done

# The conntrack guest deliberately unshares a fresh network namespace and
# brings its own loopback interface up. A global conntrack count would not
# prove ownership of the checked UDP five-tuple. Audit this functional
# prerequisite explicitly, even though it is not a separate manifest ID.
runtime_prerequisites=(NAMESPACES NET_NS)
if [[ "$target" == x86_64 ]]; then
  runtime_prerequisites+=(KVM_INTEL)
fi
for symbol in "${runtime_prerequisites[@]}"; do
  if ! grep -Fxq "CONFIG_${symbol}=y" "$out/.config"; then
    missing+=("$symbol=disabled")
    printf 'P2_KCONFIG_PREREQUISITE=BLOCKED target=%s symbol=%s\n' "$target" "$symbol" >&2
  fi
done

config_sha=$(sha256sum "$out/.config" | cut -d' ' -f1)
echo "P2_KCONFIG_RECEIPT target=$target source=$src effective_sha256=$config_sha builtins=$ready/${#required[@]} receipt=$receipt"
if (( ${#missing[@]} )); then
  printf 'P2_KCONFIG=BLOCKED_NOT_RUNTIME_PASS target=%s unresolved=%s\n' \
    "$target" "${missing[*]}" >&2
  exit 1
fi
echo "P2_KCONFIG=READY_NOT_RUNTIME_PASS target=$target builtins=$ready/${#required[@]}"
