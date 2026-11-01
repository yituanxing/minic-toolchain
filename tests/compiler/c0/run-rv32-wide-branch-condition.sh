#!/usr/bin/env bash
# Strict GNU-C vs MiniC-C RV32 ILP32 branch-truth differential.
# Tools absent => NOT_TESTED; a compile/link/QEMU error => FAIL, never PASS.
set -Eeuo pipefail

for tool in riscv64-linux-gnu-gcc riscv64-linux-gnu-ld qemu-riscv32 timeout; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "RV32_WIDE_BRANCH=NOT_TESTED missing_tool=$tool"
        exit 0
    fi
done

: "${MINIC_CC:?Set MINIC_CC to the built minic-cc executable}"
if [[ ! -x "$MINIC_CC" ]]; then
    echo 'RV32_WIDE_BRANCH=FAIL reason=minic_compiler_unavailable' >&2
    exit 1
fi

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 -std=gnu11 \
    -ffreestanding -fno-builtin -E -P -x c \
    "$root/tests/compiler/c0/rv32_wide_branch_condition.c" -o "$work/input.i"

"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
    -S "$work/input.i" -o "$work/minic.s"
riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 \
    -c "$work/minic.s" -o "$work/minic.o"
riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 -O1 -std=gnu11 \
    -ffreestanding -fno-builtin -fno-stack-protector -x c \
    -c "$work/input.i" -o "$work/gnu.o"

cat > "$work/driver.c" <<'SRC'
typedef unsigned int U32;
extern int rv32_wide_branch_value(U32, U32);
extern int rv32_wide_branch_mask(U32, U32, U32, U32);
extern int rv32_wide_branch_ternary(U32, U32);
struct Case { U32 low, high, mask_low, mask_high; };
static const struct Case cases[] = {
    {0, 0, 0xffffffffU, 0xffffffffU},
    {0, 1, 0, 1},               /* only bit 32 is true */
    {0, 1, 1, 0},               /* high value, low-only mask is false */
    {1, 0, 1, 0},
    {1, 0, 0, 1},               /* low value, high-only mask is false */
    {0, 0x80000000U, 0, 0x80000000U},
    {0xffffffffU, 0x80000001U, 1, 0},
    {0xffffffffU, 0x80000000U, 0, 0x7fffffffU},
};
int main(void) {
    unsigned i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        const struct Case *c = &cases[i];
        int nonzero = (c->low != 0U) || (c->high != 0U);
        int masked = ((c->low & c->mask_low) != 0U) ||
                     ((c->high & c->mask_high) != 0U);
        if (rv32_wide_branch_value(c->low, c->high) != nonzero) return 11;
        if (rv32_wide_branch_mask(c->low, c->high, c->mask_low,
                                 c->mask_high) != masked) return 12;
        if (rv32_wide_branch_ternary(c->low, c->high) !=
            (nonzero ? 17 : 23)) return 13;
    }
    return 0;
}
SRC

cat > "$work/start.s" <<'SRC'
.text
.globl _start
_start:
  .option push
  .option norelax
  la gp, __global_pointer$
  .option pop
  call main
  li a7, 93
  ecall
SRC

riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 -O1 -std=gnu11 \
    -ffreestanding -fno-builtin -fno-stack-protector \
    -c "$work/driver.c" -o "$work/driver.o"
riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 \
    -c "$work/start.s" -o "$work/start.o"

for mode in gnu minic; do
    riscv64-linux-gnu-ld -m elf32lriscv -static -e _start \
        -o "$work/$mode.elf" "$work/start.o" "$work/driver.o" "$work/$mode.o"
    set +e
    timeout 30s qemu-riscv32 "$work/$mode.elf"
    rc=$?
    set -e
    if [[ "$rc" -ne 0 ]]; then
        echo "RV32_WIDE_BRANCH=FAIL mode=$mode qemu_exit=$rc" >&2
        exit 1
    fi
    echo "RV32_WIDE_BRANCH_MODE=PASS mode=$mode ELF32=1 GNU_AS_LD=1 QEMU=1"
done
echo 'RV32_WIDE_BRANCH=PASS cases=8 high_only=1 low_only=1 masked_high=1 ternary=1 GNU_C_oracle=1 MiniC_C=1'
