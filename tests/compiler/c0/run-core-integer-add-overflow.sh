#!/bin/sh
set -eu

: "${MINIC:?MINIC must point to the compiler binary}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work="${BUILD_DIR:-$root/build/core-integer-add-overflow}"
source_file="$root/tests/compiler/c0/core_integer_add_overflow.c"
runtime_file="$root/tests/compiler/c0/core_integer_add_overflow_runtime.c"
mkdir -p "$work"

cc -E -P -std=gnu11 "$source_file" -o "$work/core_integer_add_overflow.i"
MINIC_CORE_IR=strict "$MINIC" -S "$work/core_integer_add_overflow.i"     -o "$work/core_integer_add_overflow-strict.s"
MINIC_CORE_CODEGEN=basic-v0 "$MINIC" -S "$work/core_integer_add_overflow.i"     -o "$work/core_integer_add_overflow-core.s"

grep -q '^core_m8_checked_int:' "$work/core_integer_add_overflow-core.s"
grep -q '^core_m8_checked_long:' "$work/core_integer_add_overflow-core.s"
grep -q '^core_m8_checked_uint:' "$work/core_integer_add_overflow-core.s"
grep -q '^core_m8_checked_ulong:' "$work/core_integer_add_overflow-core.s"
grep -q '^core_m8_size_add:' "$work/core_integer_add_overflow-core.s"
grep -q 'sltu t4, t2, t0' "$work/core_integer_add_overflow-core.s"
grep -q 'srli t4, t4, 63' "$work/core_integer_add_overflow-core.s"

"$RISCV_CC" -static -O2 "$source_file" "$runtime_file" -o "$work/reference-rv64"
"$RISCV_CC" -static -O2 "$runtime_file" "$work/core_integer_add_overflow-core.s"     -o "$work/minic-rv64"
"$QEMU_RISCV64" "$work/reference-rv64" >"$work/reference.out"
"$QEMU_RISCV64" "$work/minic-rv64" >"$work/minic.out"
cmp "$work/reference.out" "$work/minic.out"
printf '%s\n' 'PASS compiler/c0/core-integer-add-overflow'
