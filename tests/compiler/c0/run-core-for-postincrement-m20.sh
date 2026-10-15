#!/bin/sh
set -eu
: "${MINIC:?MINIC must point to the compiler binary}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work="${BUILD_DIR:-$root/build/core-for-postincrement-m20}"
source_file="$root/tests/compiler/c0/core_for_postincrement_m20.c"
runtime_file="$root/tests/compiler/c0/core_for_postincrement_m20_runtime.c"
mkdir -p "$work"
cc -E -P -std=gnu11 "$source_file" -o "$work/input.i"
MINIC_CORE_IR=strict "$MINIC" -S "$work/input.i" -o "$work/strict.s"
MINIC_CORE_CODEGEN=basic-v0 "$MINIC" -S "$work/input.i" -o "$work/core.s"
for symbol in core_m20_for_assign core_m20_postincrement core_m20_list_count_nodes; do
    grep -q "^${symbol}:" "$work/core.s"
done
"$RISCV_CC" -static -O2 "$source_file" "$runtime_file" -o "$work/reference-rv64"
"$RISCV_CC" -static -O2 "$runtime_file" "$work/core.s" -o "$work/minic-rv64"
"$QEMU_RISCV64" "$work/reference-rv64" >"$work/reference.out"
"$QEMU_RISCV64" "$work/minic-rv64" >"$work/minic.out"
cmp "$work/reference.out" "$work/minic.out"
grep -F 'post=1,42' "$work/minic.out" >/dev/null
grep -F 'assign=0,3' "$work/minic.out" >/dev/null
grep -F 'count=0,3' "$work/minic.out" >/dev/null
printf '%s\n' 'PASS compiler/c0/core-for-postincrement-m20'
