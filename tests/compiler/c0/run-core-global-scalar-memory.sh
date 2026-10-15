#!/bin/sh
set -eu

: "${MINIC:?MINIC must point to the compiler binary}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work="${BUILD_DIR:-$root/build/core-global-scalar-memory}"
source_file="$root/tests/compiler/c0/core_global_scalar_memory.c"
runtime_file="$root/tests/compiler/c0/core_global_scalar_memory_runtime.c"
mkdir -p "$work"

cc -E -P -std=gnu11 "$source_file" -o "$work/core_global_scalar_memory.i"
MINIC_CORE_IR=strict "$MINIC" -S "$work/core_global_scalar_memory.i" \
    -o "$work/core_global_scalar_memory-strict.s"
MINIC_CORE_CODEGEN=basic-v0 "$MINIC" -S "$work/core_global_scalar_memory.i" \
    -o "$work/core_global_scalar_memory-core.s"

grep -q '^core_m5_global_load:' "$work/core_global_scalar_memory-core.s"
grep -q '^core_m5_global_store:' "$work/core_global_scalar_memory-core.s"
grep -q 'la t0, core_m5_global' "$work/core_global_scalar_memory-core.s"

"$RISCV_CC" -static -O2 "$source_file" "$runtime_file" -o "$work/reference-rv64"
"$RISCV_CC" -static -O2 "$runtime_file" "$work/core_global_scalar_memory-core.s" \
    -o "$work/minic-rv64"
"$QEMU_RISCV64" "$work/reference-rv64" >"$work/reference.out"
"$QEMU_RISCV64" "$work/minic-rv64" >"$work/minic.out"
cmp "$work/reference.out" "$work/minic.out"
printf '%s\n' 'PASS compiler/c0/core-global-scalar-memory'
