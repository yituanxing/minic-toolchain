#!/usr/bin/env bash
set -Eeuo pipefail

: "${MINIC:?MINIC must point to the compiler binary}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work="${BUILD_DIR:-$root/build/core-scalar-lvalue-bitcast}"
source_file="$root/tests/compiler/c0/core_scalar_lvalue_bitcast.c"
runtime_file="$root/tests/compiler/c0/core_scalar_lvalue_bitcast_runtime.c"
mkdir -p "$work"

cc -E -P -std=gnu11 "$source_file" -o "$work/core_scalar_lvalue_bitcast.i"
MINIC_CORE_IR=strict "$MINIC" -S "$work/core_scalar_lvalue_bitcast.i" \
    -o "$work/core_scalar_lvalue_bitcast-strict.s"
MINIC_CORE_CODEGEN=basic-v0 "$MINIC" -S "$work/core_scalar_lvalue_bitcast.i" \
    -o "$work/core_scalar_lvalue_bitcast-core.s"

for symbol in core_offset_to_ptr core_pointer_read core_member_read core_pointer_bits; do
    grep -q "^${symbol}:" "$work/core_scalar_lvalue_bitcast-core.s"
    grep -q "${symbol}_core_bb0" "$work/core_scalar_lvalue_bitcast-core.s"
done

"$RISCV_CC" -static -O2 "$source_file" "$runtime_file" -o "$work/reference-rv64"
"$RISCV_CC" -static -O2 "$runtime_file" "$work/core_scalar_lvalue_bitcast-core.s" \
    -o "$work/minic-rv64"
"$QEMU_RISCV64" "$work/reference-rv64" >"$work/reference.out"
"$QEMU_RISCV64" "$work/minic-rv64" >"$work/minic.out"
cmp "$work/reference.out" "$work/minic.out"
printf '%s\n' 'PASS compiler/c0/core-scalar-lvalue-bitcast'
