#!/usr/bin/env bash
# Independent GNU-AS oracle, NOT a MiniAS fallback or MiniAS acceptance lane.
# Preserve exactly the source/config/initramfs/QEMU used by MiniAS AArch64.
set -Eeuo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$root"
version=${LINUX_VERSION:-6.6.143}
[[ $version == 6.6.143 ]] || { echo 'GNU_AARCH64_BASELINE_ERROR unsupported_version'; exit 2; }
work=${WORK_DIR:-"$root/build/minias-aarch64-gnu-oracle"}
[[ -n $work && $work != / && $work != "$root" ]] || {
  echo 'GNU_AARCH64_BASELINE_ERROR unsafe_work_directory'; exit 2;
}
mkdir -p "$work"
work=$(cd "$work" && pwd)
src="$work/linux-$version"
out="$work/out"
image="$out/arch/arm64/boot/Image"
initramfs="$work/runtime-initramfs.cpio.gz"
cc=${AARCH64_CC:-aarch64-linux-gnu-gcc}
qemu=${AARCH64_QEMU:-qemu-system-aarch64}
for bin in "$cc" aarch64-linux-gnu-as aarch64-linux-gnu-ld "$qemu" \
  curl sha256sum cpio timeout; do
  command -v "$bin" >/dev/null || {
    printf 'GNU_AARCH64_BASELINE_ERROR missing_tool=%s\n' "$bin" >&2; exit 2;
  }
done

archive="$work/linux-$version.tar.xz"
if [[ ! -s $archive ]]; then
  curl -fsSL --retry 3 --retry-delay 2 \
    "https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-$version.tar.xz" -o "$archive"
fi
printf '%s  %s\n' \
  dace1f8dc9c0dbf5df14f47e3229cd62c298e83049681731ef229f2ba7592932 \
  "$archive" | sha256sum -c -
if [[ ! -d $src ]]; then tar -xJf "$archive" -C "$work"; fi

# The same exact user-space payload producer that the MiniAS acceptance uses.
RISCV_CC="$cc" BUILD_DIR="$work/initramfs" OUTPUT_INITRAMFS="$initramfs" \
  bash tests/external/linux/build_runtime_initramfs.sh

make -C "$src" O="$out" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- defconfig
"$src/scripts/config" --file "$out/.config" \
  -e BLK_DEV_INITRD -e DEVTMPFS -e DEVTMPFS_MOUNT \
  -e SERIAL_AMBA_PL011 -e SERIAL_AMBA_PL011_CONSOLE \
  -e PROC_FS -e SYSFS -e TMPFS -e IPV6 -e IO_URING
make -C "$src" O="$out" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- olddefconfig

# The oracle does NOT expose MiniAS in PATH, -B or Kbuild AS.
started=$(date +%s)
if ! make -C "$src" O="$out" ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
  CC="$cc" AS=aarch64-linux-gnu-as -j4 Image \
  >"$work/gnu-kbuild.log" 2>&1; then
  echo 'GNU_AARCH64_BASELINE_BUILD=FAIL stage=make-Image'
  grep -nE 'error:|Error [0-9]+|undefined reference|fatal error' \
    "$work/gnu-kbuild.log" | head -n 50 || true
  tail -n 80 "$work/gnu-kbuild.log" || true
  exit 1
fi
test -s "$image"
sha256sum "$image" >"$work/Image.sha256"
sha256sum "$initramfs" >"$work/initramfs.sha256"
sha256sum "$out/.config" >"$work/config.sha256"
printf 'GNU_AARCH64_BASELINE_BUILD=PASS arch=aarch64 elapsed_s=%s GNU_AS_only=1\n' \
  "$(( $(date +%s) - started ))"

# Use the *identical* QEMU shape as the MiniAS AArch64 lane.  The P0-init
# oracle discriminates a broken QEMU/console setup from a broken MiniAS Image.
log="$work/qemu-p0-init.log"
status=0
timeout --signal=TERM "${QEMU_TIMEOUT_SECONDS:-120}" "$qemu" \
  -M virt -cpu cortex-a57 -m 1G -smp 1 \
  -nographic -no-reboot -nic none -kernel "$image" -initrd "$initramfs" \
  -append 'console=ttyAMA0 earlycon=pl011,0x9000000 loglevel=8 panic=-1 rdinit=/init' \
  </dev/null >"$log" 2>&1 || status=$?
printf 'GNU_AARCH64_BASELINE_QEMU arch=aarch64 status=%s log=%s\n' "$status" "$log"
case "$status" in 0|124|143) ;; *) tail -n 140 "$log" >&2; exit "$status" ;; esac
for marker in "Linux version $version" \
  'Kernel command line:.*rdinit=/init' \
  '(Run /init as init process|Starting init: /init)' \
  'MINIC_LINUX_RUNTIME_INIT_END fail=0' 'USER_SHELL_OK'; do
  if ! grep -E "$marker" "$log" >/dev/null; then
    printf 'GNU_AARCH64_BASELINE_RUNTIME=FAIL missing=%s\n' "$marker" >&2
    tail -n 180 "$log" >&2
    exit 1
  fi
done
echo 'GNU_AARCH64_BASELINE_RUNTIME=PASS arch=aarch64 p0_init=1 GNU_AS_only=1'
