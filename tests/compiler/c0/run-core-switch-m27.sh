#!/bin/sh
set -eu
: "${MINIC:?set MINIC}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"
: "${BUILD_DIR:=build/core-switch-m27}"
mkdir -p "$BUILD_DIR"
MINIC_CORE_IR=strict "$MINIC" -S tests/compiler/c0/core_switch_m27.c -o "$BUILD_DIR/minic.s"
"$RISCV_CC" -O0 -static tests/compiler/c0/core_switch_m27_runtime.c "$BUILD_DIR/minic.s" -o "$BUILD_DIR/minic.elf"
"$QEMU_RISCV64" "$BUILD_DIR/minic.elf"
"$RISCV_CC" -O0 -static tests/compiler/c0/core_switch_m27_runtime.c tests/compiler/c0/core_switch_m27.c -o "$BUILD_DIR/gcc.elf"
"$QEMU_RISCV64" "$BUILD_DIR/gcc.elf"
printf '%s\n' 'PASS compiler/c0/core-switch-m27'
