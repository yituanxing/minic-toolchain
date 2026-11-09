#!/bin/sh
# Host C oracle + RV32 compiler emission. Runtime requires an RV32 sysroot/QEMU.
set -eu
: "${MINIC:?Set MINIC to the absolute path of minic-cc}"
: "${CC:=cc}"

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=${BUILD_DIR:-"$root/build/core-rv32-va-record-cursor"}
source_file="$root/tests/compiler/c0/core_rv32_va_record_cursor.c"
mkdir -p "$work"

"$CC" -std=gnu11 -O2 "$source_file" -o "$work/reference-host"
"$work/reference-host"
"$CC" -std=gnu11 -E -P "$source_file" -o "$work/input.i"
"$MINIC" --target=riscv32-linux -S "$work/input.i" -o "$work/record-rv32.s"
test -s "$work/record-rv32.s"
printf '%s\n' 'PASS host GNU oracle and MiniC RV32 compile-to-assembly'

# Do not claim an RV32 ABI/runtime pass without both an actual ILP32 compiler
# and QEMU. CI can opt into the stronger differential oracle explicitly.
if [ "${RUN_RV32_RUNTIME:-0}" != 1 ]; then
    printf '%s\n' 'SKIP RV32 execution (set RUN_RV32_RUNTIME=1 with RISCV32_CC/QEMU_RISCV32)'
    exit 0
fi
: "${RISCV32_CC:?Set RISCV32_CC to an ILP32-capable cross-GCC}"
: "${QEMU_RISCV32:?Set QEMU_RISCV32 to qemu-riscv32}"
"$RISCV32_CC" -static -O2 -march=rv32i -mabi=ilp32 "$source_file" -o "$work/reference-rv32"
"$RISCV32_CC" -static -march=rv32i -mabi=ilp32 "$work/record-rv32.s" -o "$work/minic-rv32"
"$QEMU_RISCV32" "$work/reference-rv32"
"$QEMU_RISCV32" "$work/minic-rv32"
printf '%s\n' 'PASS RV32 GNU/MiniC runtime oracle'
