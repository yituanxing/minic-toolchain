#!/usr/bin/env bash
# Cross-ABI C scalar prefix/postfix semantics: RV32I/GNU-AS/MiniAS/QEMU and x86.
set -Eeuo pipefail
: "\${MINIC_CC:?set MINIC_CC}"
: "\${MINIAS:?set MINIAS}"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/volatile.c" <<'SRC'
volatile int suppress_int;
volatile signed char pending_int;
volatile int fired;
static void int_on(void) {
    __asm__ __volatile__ ("" ::: "memory");
    if (--suppress_int == 0 && pending_int)
        ++fired;
}
static int postfix(void) {
    return (suppress_int-- == 0 && pending_int) ? 9 : 3;
}
int main(void) {
    suppress_int=2;
    pending_int=1;
    fired=0;
    int_on();
    if (suppress_int!=1 || fired!=0) return 1;
    int_on();
    if (suppress_int!=0 || fired!=1) return 2;
    if (postfix()!=9 || suppress_int!=-1) return 3;
    int_on();
    if (suppress_int!=-2 || fired!=1) return 4;
    ++suppress_int;
    if (suppress_int!=-1) return 5;
    return 0;
}
SRC
cat >"$work/start.s" <<'ASM'
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
ASM
riscv64-unknown-elf-gcc -march=rv32i -mabi=ilp32 -O0 -ffreestanding \
  -E -P "$work/volatile.c" -o "$work/volatile.i"
riscv64-unknown-elf-gcc -march=rv32i -mabi=ilp32 -O0 -ffreestanding \
  -c "$work/volatile.c" -o "$work/gcc.o"
"$MINIC_CC" --target=riscv32-linux -march=rv32i -mabi=ilp32 \
  -S "$work/volatile.i" -o "$work/minic.s"
riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 -c "$work/minic.s" -o "$work/gnu-as.o"
"$MINIAS" -march=rv32i -mabi=ilp32 -o "$work/minias.o" "$work/minic.s"
riscv64-linux-gnu-gcc -march=rv32i -mabi=ilp32 -c "$work/start.s" -o "$work/start.o"
for mode in gcc gnu-as minias; do
    riscv64-linux-gnu-ld -m elf32lriscv -static -e _start \
      -o "$work/$mode.elf" "$work/start.o" "$work/$mode.o"
    if ! timeout 30s qemu-riscv32 "$work/$mode.elf"; then
        echo "CORE_VOLATILE_UPDATE=FAIL mode=$mode" >&2
        exit 1
    fi
    echo "CORE_VOLATILE_UPDATE_MODE=PASS mode=$mode"
done
# Host-native SysV uses different physical ABI and catches cross-target Core drift.
cc -O0 -c "$work/volatile.c" -o "$work/host-gcc.o"
cc -o "$work/host-gcc" "$work/host-gcc.o"
"$work/host-gcc"
cc -E -P -O0 "$work/volatile.c" -o "$work/host.i"
"$MINIC_CC" --target=x86_64-linux -S "$work/host.i" -o "$work/host.s"
cc -c "$work/host.s" -o "$work/host-minic.o"
cc -no-pie -o "$work/host-minic" "$work/host-minic.o"
"$work/host-minic"
echo 'CORE_VOLATILE_UPDATE=PASS GNU_C=1 MiniC=1 RV32_GNU_AS=1 RV32_MiniAS=1 QEMU=1 x86_native=1 prefix=1 postfix=1 short_circuit=1'
