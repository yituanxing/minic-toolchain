#!/usr/bin/env bash
# GNU GCC actively emits I only. Handwritten inline asm is NOT transformed by
# -march=rv64i; it must be rejected by a strict-I MiniAS (or GNU as).
set -Eeuo pipefail
: "${MINIAS:?MINIAS must point to minic-as}"
: "${RISCV_GCC:=riscv64-linux-gnu-gcc}"
: "${RISCV_AS:=riscv64-linux-gnu-as}"
: "${RISCV_OBJCOPY:=riscv64-linux-gnu-objcopy}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat >"$work/target.c" <<'SRC'
long sum_or_diff(long a, long b) {
    return (a + b) ^ (a - b);
}
long require_software_multiplication(long a, long b) {
    return a * b;
}
SRC

"$RISCV_GCC" -march=rv64i -mabi=lp64 -O2 -fno-pic -fno-pie \
    -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-stack-protector \
    -S "$work/target.c" -o "$work/target.s"
# The compiler never generated an M/A/CSR machine opcode; multiplication is
# a call to the runtime helper __muldi3 under an RV64I-only ISA.
if grep -Eq '^[[:space:]]*(mul|mulw|div|rem|amo[a-z.]*|csrr[a-z]*)[[:space:]]' "$work/target.s"; then
    echo 'GCC RV64I unexpectedly emitted extension opcode' >&2
    exit 1
fi
grep -q '__muldi3' "$work/target.s"
"$MINIAS" -march=rv64i -mabi=lp64 -o "$work/mini.o" "$work/target.s"
"$RISCV_AS" -march=rv64i -mabi=lp64 -o "$work/gnu.o" "$work/target.s"
"$RISCV_OBJCOPY" -j .text -O binary "$work/gnu.o" "$work/gnu.bin"
"$RISCV_OBJCOPY" -j .text -O binary "$work/mini.o" "$work/mini.bin"
cmp "$work/gnu.bin" "$work/mini.bin"

cat >"$work/inline.c" <<'SRC'
long hardware_mul(long a, long b) {
    long out;
    __asm__ volatile("mul %0, %1, %2" : "=r"(out) : "r"(a), "r"(b));
    return out;
}
SRC
"$RISCV_GCC" -march=rv64i -mabi=lp64 -O2 -S "$work/inline.c" \
    -o "$work/inline.s"
grep -Eq '^[[:space:]]*mul[[:space:]]' "$work/inline.s"
if "$MINIAS" -march=rv64i -mabi=lp64 \
    -o "$work/illegal.o" "$work/inline.s" 2>"$work/mini.err"; then
    echo 'MiniAS incorrectly accepted M extension inline asm under RV64I' >&2
    exit 1
fi
test ! -e "$work/illegal.o"
if "$RISCV_AS" -march=rv64i -mabi=lp64 \
    -o "$work/illegal-gnu.o" "$work/inline.s" 2>"$work/gnu.err"; then
    echo 'GNU as unexpectedly accepted M extension under RV64I' >&2
    exit 1
fi
echo 'MINIAS_GCC_RV64I_ISA=PASS gcc_C_to_S=1 GNU_AS_bytes=equal inline_asm_extension=blocked libgcc_helper=__muldi3'
