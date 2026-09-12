#!/usr/bin/env bash
set -Eeuo pipefail

: "${MINIC:?MINIC is required}"
: "${BUILD_DIR:?BUILD_DIR is required}"
RISCV_CC=${RISCV_CC:-riscv64-linux-gnu-gcc}
RISCV_LD=${RISCV_LD:-riscv64-linux-gnu-ld}
RISCV_NM=${RISCV_NM:-riscv64-linux-gnu-nm}
QEMU_RISCV64=${QEMU_RISCV64:-qemu-riscv64}

work="$BUILD_DIR/inline-emission-reachability"
rm -rf "$work"
mkdir -p "$work"

cat >"$work/reach.c" <<'C'
extern int minic_dead_missing_symbol(void);

static inline int dead_leaf(int x) {
    return minic_dead_missing_symbol() + x;
}

static inline int dead_parent(int x) {
    return dead_leaf(x) + 1;
}

static inline int live_leaf(int x) {
    return x + 1;
}

static inline int live_parent(int x) {
    return live_leaf(x) + 1;
}

static inline int address_root(int x) {
    return x + 3;
}

int (*address_root_ptr)(int) = address_root;

int live_entry(int x) {
    return live_parent(x);
}
C

"$MINIC" -S "$work/reach.c" -o "$work/reach.s"
"$RISCV_CC" -x assembler -c "$work/reach.s" -o "$work/reach.o"
"$RISCV_NM" "$work/reach.o" >"$work/reach.nm"
"$RISCV_NM" -u "$work/reach.o" >"$work/reach.undefined"

if grep -F 'minic_dead_missing_symbol' "$work/reach.undefined" >/dev/null; then
    echo "INLINE_EMISSION_REACHABILITY_FAIL dead-undefined-survived" >&2
    cat "$work/reach.undefined" >&2
    exit 1
fi
for symbol in live_leaf live_parent live_entry address_root address_root_ptr; do
    if ! awk -v s="$symbol" '$3==s { found=1 } END { exit found ? 0 : 1 }' "$work/reach.nm"; then
        echo "INLINE_EMISSION_REACHABILITY_FAIL missing-live-symbol=$symbol" >&2
        cat "$work/reach.nm" >&2
        exit 1
    fi
done
for symbol in dead_leaf dead_parent; do
    if awk -v s="$symbol" '$3==s { found=1 } END { exit found ? 0 : 1 }' "$work/reach.nm"; then
        echo "INLINE_EMISSION_REACHABILITY_FAIL dead-symbol-emitted=$symbol" >&2
        cat "$work/reach.nm" >&2
        exit 1
    fi
done

cat >"$work/start.s" <<'ASM'
    .section .text.start,"ax",@progbits
    .globl _start
_start:
    li a0, 40
    call live_entry
    addi a0, a0, -42
    li a7, 93
    ecall
ASM
"$RISCV_CC" -x assembler -c "$work/start.s" -o "$work/start.o"
"$RISCV_LD" -static -e _start -o "$work/reach" "$work/start.o" "$work/reach.o"
set +e
"$QEMU_RISCV64" "$work/reach"
rc=$?
set -e
test "$rc" -eq 0

echo "INLINE_EMISSION_REACHABILITY_RV64=PASS dead=discarded transitive=retained global_address=retained qemu_rc=$rc"
