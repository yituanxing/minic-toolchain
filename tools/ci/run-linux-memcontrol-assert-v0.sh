#!/usr/bin/env bash
set -Eeuo pipefail

root=$(git rev-parse --show-toplevel)
cd "$root"
work="$root/build/linux-memcontrol-assert-v0"
toolchain="$work/toolchain"
ev="$work/evidence"
mkdir -p "$ev"

python3 tools/ci/apply-local-integer-assignment-v2.py | tee "$work/local-integer-patch.log"
python3 tools/ci/apply-local-integer-logical-conditions-v0.py | tee "$work/logical-conditions-patch.log"
python3 tools/ci/apply-local-null-pointer-facts-v0.py | tee "$work/null-pointer-patch.log"
python3 tools/ci/apply-inline-integer-specialization-v0.py | tee "$work/specialization-patch.log"
python3 tools/ci/apply-inline-specialization-local-facts-v0.py | tee "$work/specialization-local-facts-patch.log"
python3 tools/ci/apply-inline-specialization-stable-parameter-facts-v0.py | tee "$work/specialization-stable-parameter-facts-patch.log"
python3 tools/ci/apply-inline-specialization-transitive-integer-v0.py | tee "$work/specialization-transitive-integer-patch.log"
python3 tools/ci/apply-inline-specialization-capacity-v0.py | tee "$work/specialization-capacity-patch.log"
python3 tools/ci/apply-inline-specialization-symbolic-address-v0.py | tee "$work/specialization-symbolic-address-patch.log"
python3 tools/ci/apply-inline-specialization-symbolic-array-lvalue-v0.py | tee "$work/specialization-symbolic-array-lvalue-patch.log"
python3 tools/ci/apply-inline-specialization-symbolic-closed-integers-v0.py | tee "$work/specialization-symbolic-closed-integers-patch.log"
python3 tools/ci/apply-inline-specialization-resolved-asm-goto-v0.py | tee "$work/specialization-resolved-asm-goto-patch.log"
python3 tools/ci/apply-inline-specialization-core-reachability-v0.py | tee "$work/specialization-core-reachability-patch.log"
python3 tools/ci/apply-inline-specialization-core-empty-function-v0.py | tee "$work/specialization-core-empty-function-patch.log"
python3 tools/ci/apply-inline-specialization-label-alias-v0.py | tee "$work/specialization-label-alias-patch.log"
git diff --check
make -j4 MODE=release CFLAGS=-Werror BUILD_DIR="$toolchain" all >/dev/null

archive="$work/linux-6.6.143.tar.xz"
src="$work/linux-6.6.143"
out="$work/out-mini"
trap 'test -s "$out/mm/memcontrol.minic-stage2.i" && cp "$out/mm/memcontrol.minic-stage2.i" "$ev/memcontrol.i" || true; test -s "$out/mm/memcontrol.minic-stage2.s" && cp "$out/mm/memcontrol.minic-stage2.s" "$ev/memcontrol.s" || true; test -s "$out/mm/memcontrol.minic-stage2.minic.stderr" && cp "$out/mm/memcontrol.minic-stage2.minic.stderr" "$ev/memcontrol.minic.stderr" || true' EXIT

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
MINIC_INLINE_SPEC_TRANSITIVE_TRACE=1 \
CORE_FAST_TRACE=1 \
  make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- \
    CC="$root/tests/external/linux/stage2_kbuild_cc.sh" \
    -j1 V=1 mm/memcontrol.o >"$ev/mini-kbuild.log" 2>&1

test -s "$out/mm/memcontrol.o"
riscv64-linux-gnu-nm -u "$out/mm/memcontrol.o" | sort -u >"$ev/memcontrol.undefined"
cp "$out/mm/memcontrol.minic-stage2.i" "$ev/memcontrol.i"
cp "$out/mm/memcontrol.minic-stage2.s" "$ev/memcontrol.s"
test ! -s "$out/mm/memcontrol.minic-stage2.minic.stderr" || \
  cp "$out/mm/memcontrol.minic-stage2.minic.stderr" "$ev/memcontrol.minic.stderr"

grep -n -C 8 '__compiletime_assert_\(781\|783\|784\|785\|786\|787\)' \
  "$ev/memcontrol.i" >"$ev/memcontrol.assert-context" || true

grep -n -C 12 '__compiletime_assert_\(781\|783\|784\|785\|786\|787\)' \
  "$ev/memcontrol.s" >"$ev/memcontrol.assert-assembly" || true

assert_count=$(grep -c '__compiletime_assert_' "$ev/memcontrol.undefined" || true)
bad_size_count=$(grep -c '__bad_size_call_parameter' "$ev/memcontrol.undefined" || true)
deferred_count=$(grep -c '__minic_deferred_asm_immediate_' "$ev/memcontrol.undefined" || true)
all_undef=$(wc -l <"$ev/memcontrol.undefined")
printf 'LINUX_MEMCONTROL_ASSERT_V0_COUNTS assert=%s bad_size=%s deferred=%s undef=%s\n' \
  "$assert_count" "$bad_size_count" "$deferred_count" "$all_undef"

grep '__compiletime_assert_' "$ev/memcontrol.undefined" || true

if [ "$bad_size_count" -ne 0 ]; then
  echo "LINUX_MEMCONTROL_ASSERT_V0=FAIL bad-size-regressed count=$bad_size_count" >&2
  exit 1
fi
if [ "$assert_count" -ne 0 ]; then
  echo "LINUX_MEMCONTROL_ASSERT_V0=FAIL assert-remains count=$assert_count" >&2
  exit 1
fi

echo "LINUX_MEMCONTROL_ASSERT_V0=PASS assert=0 deferred=$deferred_count undef=$all_undef"
