#!/usr/bin/env bash
set -Eeuo pipefail
: "${MINIC:?}"
: "${BUILD_DIR:?}"
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
cc=${RISCV_CC:-riscv64-linux-gnu-gcc}
ld=${RISCV_LD:-riscv64-linux-gnu-ld}
qemu=${QEMU_RISCV64:-qemu-riscv64}
work="$BUILD_DIR/earlycon-two-pass"
mkdir -p "$work"
src="$root/tests/compiler/c0/earlycon_two_pass_runtime.c"
cat >"$work/start.s" <<'RISCV_START'
.section .text.start,"ax",@progbits
.globl _start
_start:
    call earlycon_probe_entry
    li a7, 93
    ecall
RISCV_START
"$cc" -c -x assembler "$work/start.s" -o "$work/start.o"
"$cc" -E -P -x c "$src" -o "$work/input.i"
"$cc" -O0 -c "$work/input.i" -o "$work/gcc.o"
"$ld" -static -e _start -o "$work/gcc" "$work/start.o" "$work/gcc.o"
if ! timeout 8s "$qemu" "$work/gcc"; then
    echo "EARLYCON_TWO_PASS_GCC_CONTROL=FAIL" >&2
    exit 1
fi
echo "EARLYCON_TWO_PASS_GCC_CONTROL=PASS"
"$MINIC" -S "$work/input.i" -o "$work/minic.s"
"$cc" -c -x assembler "$work/minic.s" -o "$work/minic.o"
"$ld" -static -e _start -o "$work/minic" "$work/start.o" "$work/minic.o"
if timeout 8s "$qemu" "$work/minic"; then
    echo "EARLYCON_TWO_PASS_MINIC=PASS cases=2"
else
    rc=$?
    echo "EARLYCON_TWO_PASS_MINIC=FAIL rc=$rc gcc_control=PASS" >&2
    exit 1
fi
