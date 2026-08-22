#!/bin/sh
set -eu

: "${MINIC:?MINIC is required}"
: "${BUILD_DIR:?BUILD_DIR is required}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"
: "${HOST_CC:=cc}"

root_dir=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
source_file="$root_dir/tests/compiler/c0/core_integer_or_shifts_m22.c"
runtime_file="$root_dir/tests/compiler/c0/core_integer_or_shifts_m22_runtime.c"
mkdir -p "$BUILD_DIR"

"$HOST_CC" -E -P -std=gnu11 "$source_file" -o "$BUILD_DIR/input.i"
MINIC_CORE_IR=strict "$MINIC" -S "$BUILD_DIR/input.i" -o "$BUILD_DIR/strict.s"
MINIC_CORE_CODEGEN=basic-v0 "$MINIC" -S "$BUILD_DIR/input.i" -o "$BUILD_DIR/core.s"

grep -F 'or t0, t0, t1' "$BUILD_DIR/core.s" >/dev/null
grep -F 'sll t0, t0, t1' "$BUILD_DIR/core.s" >/dev/null
grep -F 'srl t0, t0, t1' "$BUILD_DIR/core.s" >/dev/null
grep -F 'sra t0, t0, t1' "$BUILD_DIR/core.s" >/dev/null

"$RISCV_CC" -static -O2 "$source_file" "$runtime_file" -o "$BUILD_DIR/reference-rv64"
"$RISCV_CC" -static -O2 "$runtime_file" "$BUILD_DIR/core.s" -o "$BUILD_DIR/minic-rv64"
"$QEMU_RISCV64" "$BUILD_DIR/reference-rv64" > "$BUILD_DIR/reference.out"
"$QEMU_RISCV64" "$BUILD_DIR/minic-rv64" > "$BUILD_DIR/minic.out"
diff -u "$BUILD_DIR/reference.out" "$BUILD_DIR/minic.out"
grep -Fx 'or=4660' "$BUILD_DIR/minic.out" >/dev/null
grep -Fx 'shl=4608' "$BUILD_DIR/minic.out" >/dev/null
grep -Fx 'shru=4660' "$BUILD_DIR/minic.out" >/dev/null
grep -Fx 'shrs=-1' "$BUILD_DIR/minic.out" >/dev/null
grep -Fx 'swab=13330,65280,165' "$BUILD_DIR/minic.out" >/dev/null
printf 'PASS compiler/c0/core-integer-or-shifts-m22\n'
