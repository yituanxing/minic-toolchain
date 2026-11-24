#!/bin/sh
# Shared strict Core RV64 differential: retain each fixture and its verdict.
set -eu
if [ "$#" -ne 1 ]; then
    echo "usage: $0 CASE" >&2
    exit 2
fi
case "$1" in
    core-const-parameter-m23b|core-integer-conditional-m23|core-postfix-update-m24|core-discard-expression-m25|core-integer-binary-preservation-m25b|core-integer-less-m26|core-integer-foundation-m26b|core-switch-m27|core-aggregate-boundary-m30) ;;
    *) echo "unknown strict Core runtime case: $1" >&2; exit 2 ;;
esac
name=$1
source=$(printf '%s' "$name" | tr '-' '_')
: "${MINIC:?set MINIC}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"
: "${BUILD_DIR:=build/$name}"
mkdir -p "$BUILD_DIR"
MINIC_CORE_IR=strict "$MINIC" -S "tests/compiler/c0/$source.c" -o "$BUILD_DIR/minic.s"
"$RISCV_CC" -O0 -static "tests/compiler/c0/${source}_runtime.c" "$BUILD_DIR/minic.s" -o "$BUILD_DIR/minic.elf"
if [ "$name" = core-aggregate-boundary-m30 ]; then
    "$QEMU_RISCV64" "$BUILD_DIR/minic.elf" > "$BUILD_DIR/minic.out"
else
    "$QEMU_RISCV64" "$BUILD_DIR/minic.elf"
fi
"$RISCV_CC" -O0 -static "tests/compiler/c0/${source}_runtime.c" "tests/compiler/c0/$source.c" -o "$BUILD_DIR/gcc.elf"
if [ "$name" = core-aggregate-boundary-m30 ]; then
    "$QEMU_RISCV64" "$BUILD_DIR/gcc.elf" > "$BUILD_DIR/gcc.out"
    diff -u "$BUILD_DIR/gcc.out" "$BUILD_DIR/minic.out"
else
    "$QEMU_RISCV64" "$BUILD_DIR/gcc.elf"
fi
printf '%s\n' "PASS compiler/c0/$name"
