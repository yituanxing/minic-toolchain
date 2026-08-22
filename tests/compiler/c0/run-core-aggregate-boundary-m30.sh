#!/bin/sh
set -eu
: "${MINIC:?set MINIC}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"
: "${BUILD_DIR:=build/core-aggregate-boundary-m30}"
mkdir -p "$BUILD_DIR"

MINIC_CORE_IR=strict "$MINIC" -S tests/compiler/c0/core_aggregate_boundary_m30.c -o "$BUILD_DIR/minic.s"
"$RISCV_CC" -O0 -static tests/compiler/c0/core_aggregate_boundary_m30_runtime.c "$BUILD_DIR/minic.s" -o "$BUILD_DIR/minic.elf"
"$QEMU_RISCV64" "$BUILD_DIR/minic.elf" > "$BUILD_DIR/minic.out"

"$RISCV_CC" -O0 -static tests/compiler/c0/core_aggregate_boundary_m30_runtime.c tests/compiler/c0/core_aggregate_boundary_m30.c -o "$BUILD_DIR/gcc.elf"
"$QEMU_RISCV64" "$BUILD_DIR/gcc.elf" > "$BUILD_DIR/gcc.out"

diff -u "$BUILD_DIR/gcc.out" "$BUILD_DIR/minic.out"
printf '%s\n' 'PASS compiler/c0/core-aggregate-boundary-m30'
