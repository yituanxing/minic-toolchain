#!/bin/sh
set -eu
: "${MINIC:?set MINIC}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"
: "${BUILD_DIR:=build/core-postfix-update-m24}"
mkdir -p "$BUILD_DIR"
MINIC_CORE_IR=strict "$MINIC" -S tests/compiler/c0/core_postfix_update_m24.c -o "$BUILD_DIR/minic.s"
"$RISCV_CC" -O0 -static tests/compiler/c0/core_postfix_update_m24_runtime.c "$BUILD_DIR/minic.s" -o "$BUILD_DIR/minic.elf"
"$QEMU_RISCV64" "$BUILD_DIR/minic.elf"
"$RISCV_CC" -O0 -static tests/compiler/c0/core_postfix_update_m24_runtime.c tests/compiler/c0/core_postfix_update_m24.c -o "$BUILD_DIR/gcc.elf"
"$QEMU_RISCV64" "$BUILD_DIR/gcc.elf"
printf '%s\n' 'PASS compiler/c0/core-postfix-update-m24'
