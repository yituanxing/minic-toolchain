#!/usr/bin/env bash
set -Eeuo pipefail

root=$(git rev-parse --show-toplevel)
cd "$root"
work="$root/build/linux-filter-local-integer-v2"
toolchain="$work/toolchain"
ev="$work/evidence"
mkdir -p "$ev"

python3 tools/ci/apply-local-integer-assignment-v2.py | tee "$work/patch.log"
python3 tools/ci/apply-inline-integer-specialization-v0.py | tee "$work/specialization-patch.log"
python3 tools/ci/apply-inline-specialization-local-facts-v0.py | tee "$work/specialization-local-facts-patch.log"
python3 tools/ci/apply-inline-specialization-label-alias-v0.py | tee "$work/specialization-label-alias-patch.log"
git diff --check
make -j4 MODE=release CFLAGS=-Werror BUILD_DIR="$toolchain" all >/dev/null

# Keep the existing inline-emission/reachability guard beside the Linux TU.
MINIC="$toolchain/bin/minic" BUILD_DIR="$work/regression" \
  bash tests/compiler/c0/run-inline-emission-reachability-rv64.sh

archive="$work/linux-6.6.143.tar.xz"
src="$work/linux-6.6.143"
out="$work/out-mini"
# Preserve generated assembly even when GNU as rejects it; this keeps the
# focused lane diagnostic rather than forcing another blind rerun.
trap 'test -s "$out/net/core/filter.minic-stage2.s" && cp "$out/net/core/filter.minic-stage2.s" "$ev/filter.failed.s" || true' EXIT
curl -fsSL --retry 5 --retry-delay 2 --retry-all-errors \
  https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.6.143.tar.xz -o "$archive"
printf '%s  %s\n' \
  dace1f8dc9c0dbf5df14f47e3229cd62c298e83049681731ef229f2ba7592932 \
  "$archive" | sha256sum -c -
tar -xJf "$archive" -C "$work"

make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- defconfig >/dev/null
"$src/scripts/config" --file "$out/.config" \
  -e BLK_DEV_INITRD -e DEVTMPFS -e DEVTMPFS_MOUNT \
  -e SERIAL_8250 -e SERIAL_8250_CONSOLE
make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- olddefconfig >/dev/null
make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- -j4 prepare >/dev/null

MINIC="$toolchain/bin/minic" \
REAL_CC=/usr/bin/riscv64-linux-gnu-gcc \
MINIC_KEEP_INTERMEDIATES=1 \
MINIC_KBUILD_TRACE="$ev/minic-kbuild.trace" \
CORE_FAST_TRACE=1 \
  make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- \
    CC="$root/tests/external/linux/stage2_kbuild_cc.sh" \
    -j1 V=1 net/core/filter.o >"$ev/mini-kbuild.log" 2>&1

test -s "$out/net/core/filter.o"
riscv64-linux-gnu-nm -u "$out/net/core/filter.o" | sort -u >"$ev/filter.undefined"
cp "$out/net/core/filter.minic-stage2.i" "$ev/filter.i"
cp "$out/net/core/filter.minic-stage2.s" "$ev/filter.s"

assert_count=$(grep -c '__compiletime_assert_' "$ev/filter.undefined" || true)
bad_size_count=$(grep -c '__bad_size_call_parameter' "$ev/filter.undefined" || true)
deferred_count=$(grep -c '__minic_deferred_asm_immediate_' "$ev/filter.undefined" || true)
all_undef=$(wc -l <"$ev/filter.undefined")
printf 'LINUX_FILTER_INLINE_SPECIALIZATION_V0_COUNTS assert=%s bad_size=%s deferred=%s undef=%s\n' \
  "$assert_count" "$bad_size_count" "$deferred_count" "$all_undef"

# The local-integer-only focused baseline is assert=1, bad_size=0, deferred=2, undef=164.
if [ "$assert_count" -gt 1 ]; then
  echo "LINUX_FILTER_INLINE_SPECIALIZATION_V0=FAIL assert-regressed count=$assert_count" >&2
  exit 1
fi
if [ "$bad_size_count" -ne 0 ]; then
  echo "LINUX_FILTER_INLINE_SPECIALIZATION_V0=FAIL bad-size-regressed count=$bad_size_count" >&2
  exit 1
fi

echo "LINUX_FILTER_INLINE_SPECIALIZATION_V0=PASS assert=$assert_count bad_size=$bad_size_count deferred=$deferred_count undef=$all_undef"
