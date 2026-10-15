#!/usr/bin/env bash
# Focused real RV64 GNU-vs-MiniC semantic differential for a general Core
# dead-discard rule. Zero 2064-TU Linux rebuilds; leave default MiniC unchanged.
set -Eeuo pipefail
: "${MINIC:?MINIC is required}"
: "${BUILD_DIR:?BUILD_DIR is required}"
RISCV_CC=${RISCV_CC:-riscv64-linux-gnu-gcc}
RISCV_LD=${RISCV_LD:-riscv64-linux-gnu-ld}
QEMU_RISCV64=${QEMU_RISCV64:-qemu-riscv64}
work="$BUILD_DIR/pure-discarded-address-rv64"
mkdir -p "$work"
cat >"$work/case.c" <<'C'
volatile int observed;
static int bump(void) {
    observed = observed + 1;
    return 0;
}

/* Both local address computations are effect-free, regardless of the
 * comparison's value. The discarded boolean must not reserve Core scratch. */
int pure_entry(int x) {
    int a = x;
    int b = x + 1;
    (void)(&a == &b);
    (void)(&a != &b);
    return a + b;
}

/* A comma expression can hide a call in a comparison operand.
 * This must NOT qualify for the pure-address optimization. */
int side_effect_entry(void) {
    int a = 3;
    int b = 4;
    (void)(&a == (bump(), &b));
    return observed;
}

/* A volatile read is observable even when the comparison is discarded. */
int volatile_entry(void) {
    int a = 1;
    int b = 2;
    volatile int *volatile target = &b;
    (void)(&a == target);
    return 13;
}
C
"$RISCV_CC" -E -P -x c "$work/case.c" -o "$work/case.i"

# The old and new code paths must compile the EXACT same preprocessed bytes.
env -u MINIC_OPT_PURE_DISCARDED_ADDRESS_EQ "$MINIC" -S "$work/case.i" -o "$work/baseline.s"
MINIC_OPT_PURE_DISCARDED_ADDRESS_EQ=1 "$MINIC" -S "$work/case.i" -o "$work/optimized.s"
test -s "$work/baseline.s" && test -s "$work/optimized.s"
if cmp -s "$work/baseline.s" "$work/optimized.s"; then
    echo "PURE_DISCARD_RV64=FAIL opt-in emitted no change for a direct local comparison" >&2
    exit 1
fi

cat >"$work/start.s" <<'ASM'
    .section .text.start,"ax",@progbits
    .globl _start
_start:
    li a0, 3
    call pure_entry
    li t0, 7
    bne a0, t0, .Lfail
    call side_effect_entry
    li t0, 1
    bne a0, t0, .Lfail
    call volatile_entry
    li t0, 13
    bne a0, t0, .Lfail
    li a0, 0
    j .Lexit
.Lfail:
    li a0, 1
.Lexit:
    li a7, 93
    ecall
ASM
"$RISCV_CC" -x assembler -c "$work/start.s" -o "$work/start.o"

# Run all THREE executable variants; semantic agreement is mandatory.
for kind in baseline optimized; do
    "$RISCV_CC" -x assembler -c "$work/$kind.s" -o "$work/$kind.o"
    "$RISCV_LD" -static -e _start -o "$work/$kind.elf" "$work/start.o" "$work/$kind.o"
    "$QEMU_RISCV64" "$work/$kind.elf"
done
"$RISCV_CC" -O0 -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
    -c "$work/case.c" -o "$work/gcc.o"
"$RISCV_LD" -static -e _start -o "$work/gcc.elf" "$work/start.o" "$work/gcc.o"
"$QEMU_RISCV64" "$work/gcc.elf"

# No live externals/volatile reads may be silently erased by this Core rule.
echo "PURE_DISCARD_RV64=PASS GCC=RUN_OK old_MiniC=RUN_OK new_MiniC=RUN_OK pure_elision=PRODUCER_BYTES_CHANGED side_effect=RETAINED volatile=RETAINED"
