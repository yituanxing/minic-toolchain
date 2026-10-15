#!/bin/sh
set -eu

: "${MINIC:?MINIC is required}"
: "${BUILD_DIR:?BUILD_DIR is required}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"
: "${HOST_CC:=cc}"

root_dir=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
source_file="$root_dir/tests/compiler/c0/core_pointer_condition_m21.c"
runtime_file="$root_dir/tests/compiler/c0/core_pointer_condition_m21_runtime.c"
mkdir -p "$BUILD_DIR"

"$HOST_CC" -E -P -std=gnu11 "$source_file" -o "$BUILD_DIR/input.i"
MINIC_CORE_IR=strict "$MINIC" -S "$BUILD_DIR/input.i" -o "$BUILD_DIR/strict.s"
MINIC_CORE_CODEGEN=basic-v0 "$MINIC" -S "$BUILD_DIR/input.i" -o "$BUILD_DIR/core.s"

"$RISCV_CC" -static -O2 "$source_file" "$runtime_file" -o "$BUILD_DIR/reference-rv64"
"$RISCV_CC" -static -O2 "$runtime_file" "$BUILD_DIR/core.s" -o "$BUILD_DIR/minic-rv64"

"$QEMU_RISCV64" "$BUILD_DIR/reference-rv64" > "$BUILD_DIR/reference.out"
"$QEMU_RISCV64" "$BUILD_DIR/minic-rv64" > "$BUILD_DIR/minic.out"
diff -u "$BUILD_DIR/reference.out" "$BUILD_DIR/minic.out"
grep -Fx 'if=7,3' "$BUILD_DIR/minic.out" >/dev/null
grep -Fx 'not=5,11' "$BUILD_DIR/minic.out" >/dev/null
grep -Fx 'hlist=1,1,1' "$BUILD_DIR/minic.out" >/dev/null
printf 'PASS compiler/c0/core-pointer-condition-m21\n'
