#!/bin/sh
set -eu

: "${MINIC:?MINIC must point to the compiler binary}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work="${BUILD_DIR:-$root/build/core-fixed-call-scalar-conversions}"
source_file="$root/tests/compiler/c0/core_fixed_call_scalar_conversions.c"
runtime_file="$root/tests/compiler/c0/core_fixed_call_scalar_conversions_runtime.c"
mkdir -p "$work"

cc -E -P -std=gnu11 "$source_file" -o "$work/core_fixed_call_scalar_conversions.i"
MINIC_CORE_IR=strict "$MINIC" -S "$work/core_fixed_call_scalar_conversions.i" \
    -o "$work/core_fixed_call_scalar_conversions-strict.s"
MINIC_CORE_CODEGEN=basic-v0 "$MINIC" -S "$work/core_fixed_call_scalar_conversions.i" \
    -o "$work/core_fixed_call_scalar_conversions-core.s"

for symbol in \
    core_m3_integer_conversion \
    core_m3_pointer_qualification \
    core_m3_null_pointer \
    core_m3_pointer_bool \
    core_m3_read_word_at_a_time; do
    grep -q "^${symbol}:" "$work/core_fixed_call_scalar_conversions-core.s"
    grep -q "${symbol}_core_bb0" "$work/core_fixed_call_scalar_conversions-core.s"
done

"$RISCV_CC" -static -O2 "$source_file" "$runtime_file" -o "$work/reference-rv64"
"$RISCV_CC" -static -O2 "$runtime_file" "$work/core_fixed_call_scalar_conversions-core.s" \
    -o "$work/minic-rv64"
"$QEMU_RISCV64" "$work/reference-rv64" >"$work/reference.out"
"$QEMU_RISCV64" "$work/minic-rv64" >"$work/minic.out"
cmp "$work/reference.out" "$work/minic.out"
printf '%s\n' 'PASS compiler/c0/core-fixed-call-scalar-conversions'
