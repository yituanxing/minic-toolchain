#!/usr/bin/env bash
set -Eeuo pipefail

root=$(git rev-parse --show-toplevel)
cd "$root"
work="$root/build/constant-cfg-v0"
toolchain="$work/toolchain"
testdir="$work/test"
mkdir -p "$testdir"

python3 tools/ci/apply-constant-cfg-product-v0.py | tee "$work/patch.log"
git diff --check

make -j4 MODE=release CFLAGS=-Werror BUILD_DIR="$toolchain" all

cat >"$testdir/switch.c" <<'C'
extern int should_not_exist(void);
int constant_switch_probe(void) {
    switch (sizeof(long)) {
    case 1:
        return should_not_exist();
    case 8:
        return 47;
    default:
        return should_not_exist();
    }
}
C
riscv64-linux-gnu-gcc -E -P -std=gnu11 -x c "$testdir/switch.c" -o "$testdir/switch.i"
"$toolchain/bin/minic" -S "$testdir/switch.i" -o "$testdir/switch.s"
riscv64-linux-gnu-gcc -x assembler -c "$testdir/switch.s" -o "$testdir/switch.o"
riscv64-linux-gnu-nm -u "$testdir/switch.o" >"$testdir/switch.undefined"
if grep -F should_not_exist "$testdir/switch.undefined"; then
    printf '%s\n' 'CONSTANT_SWITCH_CFG_FAIL dead-symbol-survived' >&2
    exit 1
fi

cat >"$testdir/start.s" <<'ASM'
    .section .text.start,"ax",@progbits
    .globl _start
_start:
    call constant_switch_probe
    addi a0, a0, -47
    li a7, 93
    ecall
ASM
riscv64-linux-gnu-gcc -x assembler -c "$testdir/start.s" -o "$testdir/start.o"
riscv64-linux-gnu-ld -static -e _start -o "$testdir/switch" "$testdir/start.o" "$testdir/switch.o"
set +e
qemu-riscv64 "$testdir/switch"
rc=$?
set -e
if [ "$rc" -ne 0 ]; then
    printf 'CONSTANT_SWITCH_CFG_FAIL qemu_rc=%s\n' "$rc" >&2
    exit 1
fi
printf 'MINIC_CORE_CONSTANT_SWITCH_V0=PASS dead_ref=discarded qemu_rc=%s\n' "$rc"

MINIC="$toolchain/bin/minic" \
BUILD_DIR="$work/regression" \
    bash tests/compiler/c0/run-inline-emission-reachability-rv64.sh

if ! git diff --quiet -- src/core/core_lower.c src/target/riscv64/core_codegen.c; then
    git config user.name "github-actions[bot]"
    git config user.email "41898282+github-actions[bot]@users.noreply.github.com"
    git add src/core/core_lower.c src/target/riscv64/core_codegen.c
    git commit -m "core: prune constant-switch dead CFG [constant-cfg-product]"
    git push origin HEAD:agent/linux-link-correctness-v0
    printf '%s\n' 'MINIC_CORE_CONSTANT_SWITCH_PRODUCT=PUSHED'
else
    printf '%s\n' 'MINIC_CORE_CONSTANT_SWITCH_PRODUCT=ALREADY_LANDED'
fi
