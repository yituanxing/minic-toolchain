#!/usr/bin/env bash
# True C -> Core -> RV32I -> GNU AS ELF32 -> GNU LD -> QEMU Linux.
# The same MiniC source remains fail-closed on unsupported wider ABI.
set -Eeuo pipefail
: "${MINIC_CC:?}"
: "${MINIAS:=${MINIC_CC%/minic-cc}/minic-as}"
test -x "$MINIAS"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/core.i" <<'SRC'
int add(int a, int b) { return a + b; }
int mul(int a, int b) { return a * b; }
int absdiff(int a, int b) {
    int x = a - b;
    if (x < 0) x = -x;
    return x;
}
int triangular(int n) {
    int total = 0;
    while (n > 0) {
        total = total + n;
        n = n - 1;
    }
    return total;
}
int caller(int a, int b) {
    return add(a,b) + mul(a,b) + absdiff(a,b);
}
SRC
"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
    -S "$work/core.i" -o "$work/core.s"
riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 -c \
    "$work/core.s" -o "$work/core.o"
"$MINIAS" -march=rv32i -mabi=ilp32 \
    -o "$work/core-native.o" "$work/core.s"
riscv64-linux-gnu-readelf -h "$work/core-native.o" |
  grep -q 'Class:.*ELF32'
cat >"$work/start.s" <<'SRC'
.text
.globl _start
_start:
    li a0, 6
    li a1, 3
    call caller
    li t0, 30
    bne a0, t0, fail
    li a0, 5
    call triangular
    li t0, 15
    bne a0, t0, fail
    li a0, 0
    j exit
fail:
    li a0, 1
exit:
    li a7, 93
    ecall
SRC
riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 -c "$work/start.s" -o "$work/start.o"
riscv64-linux-gnu-ld -m elf32lriscv -static -e _start \
    -o "$work/prog.elf" "$work/start.o" "$work/core.o"
qemu-riscv32 "$work/prog.elf"
# Second independent assembly producer for identical MiniC/Core C program.
# The GNU linker is retained as the oracle because generic MiniLD ELF32
# program linking is not yet qualified (only compat-vDSO is certified).
riscv64-linux-gnu-ld -m elf32lriscv -static -e _start \
    -o "$work/prog-native.elf" "$work/start.o" "$work/core-native.o"
qemu-riscv32 "$work/prog-native.elf"
echo 'RV32_NATIVE_CHAIN=PASS minic_C=true minic_as_ELF32=true GNU_LD=true qemu_riscv32=true'
riscv64-linux-gnu-readelf -h "$work/prog.elf" |
  grep -q 'Class:.*ELF32'
# RV32 frontend must not claim multi-XLEN scalar arithmetic prematurely.
cat >"$work/wide.i" <<'SRC'
long long wide(long long x) { return x + 1; }
SRC
if "$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
    -S "$work/wide.i" -o "$work/wide.s" 2>"$work/wide.err"; then
    echo 'RV32_CORE=FAIL unexpectedly accepted wide return ABI' >&2
    exit 1
fi
grep -Eq 'rv32i:|not yet qualified' "$work/wide.err"
echo 'RV32_CORE=PASS C_functions=5 ILP32=1 ELF32=1 QEMU=PASS GNU_AS=PASS MiniAS=PASS GNU_LD=PASS RV32I_Mless_mul=PASS wide_ABI=reject'
