#!/bin/sh
set -eu
: "${MINIC:?MINIC must point to the compiler binary}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work="${BUILD_DIR:-$root/build/core-pointer-offset}"
source_file="$root/tests/compiler/c0/core_pointer_offset.c"
runtime_file="$root/tests/compiler/c0/core_pointer_offset_runtime.c"
mkdir -p "$work"
cc -E -P -std=gnu11 "$source_file" -o "$work/input.i"
MINIC_CORE_IR=strict "$MINIC" -S "$work/input.i" -o "$work/strict.s"
MINIC_CORE_CODEGEN=basic-v0 "$MINIC" -S "$work/input.i" -o "$work/core.s"
for symbol in core_m15_void_add core_m15_int_add core_m15_int_add_commuted core_m15_list_poison; do
    grep -q "^${symbol}:" "$work/core.s"
done
"$RISCV_CC" -static -O2 -std=gnu11 "$source_file" "$runtime_file" -o "$work/reference-rv64"
"$RISCV_CC" -static -O2 "$runtime_file" "$work/core.s" -o "$work/minic-rv64"
"$QEMU_RISCV64" "$work/reference-rv64" >"$work/reference.out"
"$QEMU_RISCV64" "$work/minic-rv64" >"$work/minic.out"
cmp "$work/reference.out" "$work/minic.out"
printf '%s\n' 'PASS compiler/c0/core-pointer-offset'
