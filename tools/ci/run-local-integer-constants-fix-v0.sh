#!/usr/bin/env bash
set -Eeuo pipefail

root=$(git rev-parse --show-toplevel)
cd "$root"
work="$root/build/local-integer-constants-v0"
toolchain="$work/toolchain"
testdir="$work/test"
ev="$work/evidence"
mkdir -p "$testdir" "$ev"

python3 tools/ci/apply-local-integer-constants-v0.py | tee "$work/patch.log"
python3 tools/ci/patch-local-read-lvalue-v0.py | tee -a "$work/patch.log"
git diff --check

make -j4 MODE=release CFLAGS=-Werror BUILD_DIR="$toolchain" all

cat >"$testdir/local-constants.c" <<'C'
extern int should_not_exist(void);

int constant_local_probe(void) {
    const int size = ({
        int selected = -22;
        if (sizeof(long) == 1)
            selected = 16;
        else if (sizeof(long) == 2)
            selected = 8;
        else if (sizeof(long) == 4)
            selected = 0;
        else if (sizeof(long) == 8)
            selected = 24;
        selected;
    });
    if (!(!(size < 0)))
        return should_not_exist();
    return size;
}

int nonconstant_merge_probe(int choose) {
    int value = 3;
    if (choose)
        value = 7;
    return value;
}

int escaped_local_probe(void) {
    int value = 4;
    int *pointer = &value;
    *pointer = 9;
    return value;
}
C

riscv64-linux-gnu-gcc -E -P -std=gnu11 -x c "$testdir/local-constants.c" \
    -o "$testdir/local-constants.i"
"$toolchain/bin/minic" -S "$testdir/local-constants.i" -o "$testdir/local-constants.s"
riscv64-linux-gnu-gcc -x assembler -c "$testdir/local-constants.s" \
    -o "$testdir/local-constants.o"
riscv64-linux-gnu-nm -u "$testdir/local-constants.o" \
    >"$testdir/local-constants.undefined"
if grep -F should_not_exist "$testdir/local-constants.undefined"; then
    echo 'LOCAL_INTEGER_CONSTANTS_FAIL compiletime-dead-reference-survived' >&2
    exit 1
fi

cat >"$testdir/start.s" <<'ASM'
    .section .text.start,"ax",@progbits
    .globl _start
_start:
    call constant_local_probe
    li t0, 24
    bne a0, t0, 1f

    li a0, 1
    call nonconstant_merge_probe
    li t0, 7
    bne a0, t0, 2f

    call escaped_local_probe
    li t0, 9
    bne a0, t0, 3f

    li a0, 0
    li a7, 93
    ecall
1:
    li a0, 41
    li a7, 93
    ecall
2:
    li a0, 42
    li a7, 93
    ecall
3:
    li a0, 43
    li a7, 93
    ecall
ASM
riscv64-linux-gnu-gcc -x assembler -c "$testdir/start.s" -o "$testdir/start.o"
riscv64-linux-gnu-ld -static -e _start -o "$testdir/local-constants" \
    "$testdir/start.o" "$testdir/local-constants.o"
set +e
qemu-riscv64 "$testdir/local-constants"
rc=$?
set -e
if [ "$rc" -ne 0 ]; then
    echo "LOCAL_INTEGER_CONSTANTS_FAIL qemu_rc=$rc" >&2
    exit 1
fi
echo "MINIC_LOCAL_INTEGER_CONSTANTS_SYNTHETIC=PASS qemu_rc=$rc"

MINIC="$toolchain/bin/minic" \
BUILD_DIR="$work/regression" \
    bash tests/compiler/c0/run-inline-emission-reachability-rv64.sh

archive="$work/linux-6.6.143.tar.xz"
src="$work/linux-6.6.143"
out="$work/out-mini"
curl -fsSL --retry 5 --retry-delay 2 --retry-all-errors \
    https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.6.143.tar.xz \
    -o "$archive"
printf '%s  %s\n' \
    dace1f8dc9c0dbf5df14f47e3229cd62c298e83049681731ef229f2ba7592932 \
    "$archive" | sha256sum -c -
tar -xJf "$archive" -C "$work"
make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- defconfig
"$src/scripts/config" --file "$out/.config" \
    -e BLK_DEV_INITRD -e DEVTMPFS -e DEVTMPFS_MOUNT \
    -e SERIAL_8250 -e SERIAL_8250_CONSOLE
make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- olddefconfig
make -C "$src" O="$out" ARCH=riscv CROSS_COMPILE=riscv64-linux-gnu- -j4 prepare
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
echo "LINUX_FILTER_LOCAL_CONSTANTS_COUNTS assert=$assert_count bad_size=$bad_size_count deferred=$deferred_count"

if [ "$assert_count" -ge 180 ]; then
    echo "LOCAL_INTEGER_CONSTANTS_FAIL insufficient-filter-improvement assert=$assert_count" >&2
    exit 1
fi
if [ "$bad_size_count" -ne 0 ]; then
    echo "LOCAL_INTEGER_CONSTANTS_FAIL bad-size-regressed count=$bad_size_count" >&2
    exit 1
fi

echo "LINUX_FILTER_LOCAL_INTEGER_CONSTANTS=PASS assert=$assert_count bad_size=$bad_size_count deferred=$deferred_count"

if ! git diff --quiet -- src/core/core_lower_internal.h src/core/core_lower.c; then
    git config user.name "github-actions[bot]"
    git config user.email "41898282+github-actions[bot]@users.noreply.github.com"
    git add src/core/core_lower_internal.h src/core/core_lower.c
    git commit -m "core: track conservative local integer constants [local-constants-product]"
    git push origin HEAD:agent/linux-link-correctness-v0
    echo "MINIC_LOCAL_INTEGER_CONSTANTS_PRODUCT=PUSHED sha=$(git rev-parse HEAD)"
else
    echo 'MINIC_LOCAL_INTEGER_CONSTANTS_PRODUCT=ALREADY_LANDED'
fi
