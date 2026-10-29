#!/usr/bin/env bash
# Original Linux 6.6.143 GNU Kbuild discovery: x86-64/AArch64/LoongArch64
# ONE reusable script and three target-specific upstream GNU defconfigs.
# Build and C-owner discovery ONLY. Boot, MiniC ownership and P1 are NOT PASS.
set -Eeuo pipefail
if (( $# != 2 )); then
  echo "usage: $0 <x86_64|aarch64|loongarch64> <new-work-dir>" >&2
  exit 64
fi
target=$1
work=$2
case "$target" in
  x86_64)
    arch=x86; cross=; gcc=gcc; config=x86_64_defconfig
    build_target=bzImage; image=arch/x86/boot/bzImage ;;
  aarch64)
    arch=arm64; cross=aarch64-linux-gnu-; gcc=aarch64-linux-gnu-gcc
    config=defconfig; build_target=Image; image=arch/arm64/boot/Image ;;
  loongarch64)
    arch=loongarch; cross=loongarch64-linux-gnu-
    gcc=loongarch64-linux-gnu-gcc-13; config=loongson3_defconfig
    build_target=vmlinux; image=vmlinux ;;
  *) echo "MINIC_LINUX_THREE_GNU=FAIL unsupported target=$target" >&2; exit 64 ;;
esac
if [[ -d "$work" ]] && find "$work" -mindepth 1 -maxdepth 1 | grep -q .; then
  echo "MINIC_LINUX_THREE_GNU=FAIL refuse nonempty old work tree=$work" >&2
  exit 2
fi
mkdir -p "$work"
work=$(cd "$work" && pwd)
src="$work/linux-6.6.143"
out="$work/build"
evidence="$work/evidence"
mkdir -p "$out" "$evidence"
kernel="$work/linux-6.6.143.tar.xz"
# The same SHA-bound original upstream kernel tarball used by historical RV64.
curl -fLSs --retry 4 --retry-delay 2 \
  https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.6.143.tar.xz -o "$kernel"
printf '%s  %s\n' \
  dace1f8dc9c0dbf5df14f47e3229cd62c298e83049681731ef229f2ba7592932 \
  "$kernel" | sha256sum -c -
sha256sum "$kernel" >"$evidence/linux-source.sha256"
tar -xJf "$kernel" -C "$work"
gcc_target=$("$gcc" -dumpmachine)
case "$target:$gcc_target" in
  x86_64:x86_64*|aarch64:aarch64*|loongarch64:loongarch64*) ;;
  *) echo "MINIC_LINUX_THREE_GNU=FAIL wrong GNU target=$gcc_target" >&2; exit 2 ;;
esac
"$gcc" --version | head -n 1 > "$evidence/gnu-compiler-version.txt"
printf '%s\n' \
  "source=linux-6.6.143" "target=$target" "arch=$arch" \
  "gnu_cc=$gcc" "gnu_triplet=$gcc_target" "cross_prefix=$cross" \
  "vendor_defconfig_reference=$config" "build_target=$build_target" "image=$image" \
  "profile=${MINIC_LINUX_CONFIG_PROFILE:-broad}" \
  "verdict=GNU_BUILD_ONLY_NOT_QEMU_CERTIFIED_NOT_MINIC" \
  >"$evidence/build-contract.txt"
export KBUILD_BUILD_USER=minic KBUILD_BUILD_HOST=minic-ci
export KBUILD_BUILD_TIMESTAMP='Thu Jan  1 00:00:00 UTC 1970'
# Default to each target's original upstream defconfig for full-source breadth.
# The shared allnoconfig feature profile remains available only as an explicit
# reduced-scope smoke; Kconfig resolves target platform symbols independently.
profile=${MINIC_LINUX_CONFIG_PROFILE:-broad}
minimum=1000
case "$profile" in
  common)
    common_config="$PWD/tools/ci/linux-three-isa-common.config"
    [[ -s "$common_config" ]] || {
      echo "MINIC_LINUX_KCONFIG=FAIL missing common config=$common_config" >&2
      exit 2
    }
    printf '%s\n' "shared_policy_sha256=$(sha256sum "$common_config" | cut -d' ' -f1)" >>"$evidence/build-contract.txt"
    minimum=200
    config=allnoconfig
    # Kernel-supported miniconfig semantics; do not hand-edit Kconfig symbols.
    make -C "$src" O="$out" ARCH="$arch" CROSS_COMPILE="$cross" CC="$gcc" \
      KCONFIG_ALLCONFIG="$common_config" allnoconfig 2>&1 | tee "$evidence/kconfig.log"
    ;;
  broad)
    # Optional original vendor defconfig as a later breadth/stress campaign.
    make -C "$src" O="$out" ARCH="$arch" CROSS_COMPILE="$cross" CC="$gcc" \
      "$config" 2>&1 | tee "$evidence/kconfig.log"
    ;;
  *)
    echo "MINIC_LINUX_KCONFIG=FAIL unknown profile=$profile" >&2
    exit 64
    ;;
esac
make -C "$src" O="$out" ARCH="$arch" CROSS_COMPILE="$cross" CC="$gcc" \
  olddefconfig 2>&1 | tee -a "$evidence/kconfig.log"
sha256sum "$out/.config" >"$evidence/config.sha256"
if [[ "$profile" == common ]]; then
  python3 tools/ci/linux-three-isa-golden-v1.py \
    --verify-common-config --common-config "$common_config" \
    --effective-config "$out/.config" --evidence "$evidence"
fi
make -C "$src" O="$out" ARCH="$arch" CROSS_COMPILE="$cross" CC="$gcc" \
  -j4 "$build_target" 2>&1 | tee "$evidence/gnu-kbuild.log"
test -s "$out/$image" || {
  echo "MINIC_LINUX_THREE_GNU=FAIL image_missing=$image" >&2; exit 2;
}
python3 tools/ci/linux-three-isa-golden-v1.py \
  --target "$target" --out "$out" --image "$image" --evidence "$evidence" \
  --min-c-objects "$minimum" --config-profile "$profile"
# Freeze the canonical GNU -E -P output ONCE with the exact .o.cmd flags.
# This 7-worker producer is independent of the MiniC candidate revision.
# Seven per-target MiniC shard runners can reuse these .i via Actions cache.
python3 tools/ci/linux-three-isa-freeze-i-v1.py freeze \
  --target "$target" --gcc "$gcc" --out "$out" \
  --evidence "$evidence" --frozen "$work/frozen" --workers 7
echo "MINIC_LINUX_THREE_DISCOVERY=GNU_BUILD_ONLY target=$target GNU_elf_link=1 QEMU=NOT_RUN MiniC=NOT_RUN"
echo "MINIC_LINUX_THREE_I=FROZEN target=$target shards=7 cache_reusable=1"
