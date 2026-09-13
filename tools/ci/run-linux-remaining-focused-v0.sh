#!/usr/bin/env bash
set -Eeuo pipefail

root=$(git rev-parse --show-toplevel)
work="$root/build/linux-remaining-focused-v0"
ev="$work/evidence"
build="$work/toolchain"
archive="$work/linux-6.6.143.tar.xz"
src="$work/linux-6.6.143"
out="$work/out-mini"
wrapper="$root/tests/external/linux/stage2_kbuild_cc.sh"

rm -rf "$work"
mkdir -p "$ev"

python3 tools/ci/apply-local-integer-assignment-v2.py | tee "$ev/local-integer-patch.log"
python3 tools/ci/apply-local-integer-logical-conditions-v0.py | tee "$ev/local-integer-logical-conditions-patch.log"
python3 tools/ci/apply-local-null-pointer-facts-v0.py | tee "$ev/local-null-pointer-facts-patch.log"
python3 tools/ci/apply-inline-integer-specialization-v0.py | tee "$ev/inline-integer-specialization-patch.log"
python3 tools/ci/apply-inline-specialization-local-facts-v0.py | tee "$ev/inline-specialization-local-facts-patch.log"
python3 tools/ci/apply-inline-specialization-stable-parameter-facts-v0.py | tee "$ev/inline-specialization-stable-parameter-facts-patch.log"
python3 tools/ci/apply-inline-specialization-transitive-integer-v0.py | tee "$ev/inline-specialization-transitive-integer-patch.log"
python3 tools/ci/apply-inline-specialization-capacity-v0.py | tee "$ev/inline-specialization-capacity-patch.log"
python3 tools/ci/apply-inline-specialization-symbolic-address-v0.py | tee "$ev/inline-specialization-symbolic-address-patch.log"
python3 tools/ci/apply-inline-specialization-symbolic-array-lvalue-v0.py | tee "$ev/inline-specialization-symbolic-array-lvalue-patch.log"
python3 tools/ci/apply-inline-specialization-symbolic-closed-integers-v0.py | tee "$ev/inline-specialization-symbolic-closed-integers-patch.log"
python3 tools/ci/apply-inline-specialization-resolved-asm-goto-v0.py | tee "$ev/inline-specialization-resolved-asm-goto-patch.log"
python3 tools/ci/apply-inline-specialization-core-reachability-v0.py | tee "$ev/inline-specialization-core-reachability-patch.log"
python3 tools/ci/apply-inline-specialization-core-empty-function-v0.py | tee "$ev/inline-specialization-core-empty-function-patch.log"
python3 tools/ci/apply-inline-specialization-label-alias-v0.py | tee "$ev/inline-specialization-label-alias-patch.log"
git diff --check
make -j4 MODE=release CFLAGS=-Werror BUILD_DIR="$build" all

curl -fsSL --retry 5 --retry-delay 2 --retry-all-errors \
  https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.6.143.tar.xz -o "$archive"
printf '%s  %s\n' \
  dace1f8dc9c0dbf5df14f47e3229cd62c298e83049681731ef229f2ba7592932 \
  "$archive" | sha256sum -c -
tar -xJf "$archive" -C "$work"
mkdir -p "$out"
make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- defconfig
"$src/scripts/config" --file "$out/.config" \
  -e BLK_DEV_INITRD -e DEVTMPFS -e DEVTMPFS_MOUNT \
  -e SERIAL_8250 -e SERIAL_8250_CONSOLE
make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- olddefconfig
make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- -j4 prepare

targets=(
  net/core/dev.o
  arch/riscv/kernel/cpufeature.o
  net/ipv4/tcp_output.o
  kernel/sched/core.o
)

MINIC="$build/bin/minic" \
REAL_CC=/usr/bin/riscv64-linux-gnu-gcc \
MINIC_KEEP_INTERMEDIATES=1 \
MINIC_KBUILD_TRACE="$ev/minic-kbuild.trace" \
CORE_FAST_TRACE=1 \
  make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- \
    CC="$wrapper" -j1 V=1 "${targets[@]}" >"$ev/mini-kbuild.log" 2>&1

for target in "${targets[@]}"; do
  key=${target//\//__}
  key=${key%.o}
  stem=${target%.o}
  obj="$out/$target"
  test -s "$obj"
  riscv64-linux-gnu-nm -u "$obj" | sed -E 's/^[[:space:]]*[A-Za-z]?[[:space:]]*//' | sed '/^$/d' | sort -u >"$ev/$key.undefined"
  grep -E '^(__compiletime_assert_|__minic_deferred_asm_immediate_|__bad_size_call_parameter$)' "$ev/$key.undefined" >"$ev/$key.suspicious" || true
  for suffix in i s minic.stdout minic.stderr; do
    f="$out/$stem.minic-stage2.$suffix"
    if test -f "$f"; then cp "$f" "$ev/$key.minic-stage2.$suffix"; fi
  done
  if test -f "$ev/$key.minic-stage2.i"; then
    grep -n -C 4 '__compiletime_assert_' "$ev/$key.minic-stage2.i" >"$ev/$key.assert-context.txt" || true
  fi
  if test -f "$ev/$key.minic-stage2.s"; then
    grep -n -C 8 '__minic_deferred_asm_immediate_' "$ev/$key.minic-stage2.s" >"$ev/$key.deferred-context.txt" || true
  fi
  printf 'REMAINING_FOCUSED target=%s suspicious=%s\n' "$target" "$(wc -l < "$ev/$key.suspicious")"
  cat "$ev/$key.suspicious"
done

echo "LINUX_REMAINING_FOCUSED_V0=PASS objects=${#targets[@]}"
