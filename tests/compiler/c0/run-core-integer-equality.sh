#!/bin/sh
set -eu

: "${MINIC:?MINIC must point to the compiler binary}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work="${BUILD_DIR:-$root/build/core-integer-equality}"
source_file="$root/tests/compiler/c0/core_integer_equality.c"
runtime_file="$root/tests/compiler/c0/core_integer_equality_runtime.c"
mkdir -p "$work"

cc -E -P -std=gnu11 "$source_file" -o "$work/core_integer_equality.i"
MINIC_CORE_IR=strict "$MINIC" -S "$work/core_integer_equality.i" \
    -o "$work/core_integer_equality-strict.s"
MINIC_CORE_CODEGEN=basic-v0 "$MINIC" -S "$work/core_integer_equality.i" \
    -o "$work/core_integer_equality-core.s"

grep -q '^core_m5b_equal:' "$work/core_integer_equality-core.s"
grep -q '^core_m5b_set_if_equal:' "$work/core_integer_equality-core.s"
grep -q '^core_m11_pointer_equal:' "$work/core_integer_equality-core.s"
grep -q '^core_m11_member_pointer_equal:' "$work/core_integer_equality-core.s"
grep -q 'xor t0, t0, t1' "$work/core_integer_equality-core.s"
grep -q 'seqz t0, t0' "$work/core_integer_equality-core.s"
grep -q 'la t0, core_m5b_global' "$work/core_integer_equality-core.s"

"$RISCV_CC" -static -O2 "$source_file" "$runtime_file" -o "$work/reference-rv64"
"$RISCV_CC" -static -O2 "$runtime_file" "$work/core_integer_equality-core.s" \
    -o "$work/minic-rv64"
"$QEMU_RISCV64" "$work/reference-rv64" >"$work/reference.out"
"$QEMU_RISCV64" "$work/minic-rv64" >"$work/minic.out"
cmp "$work/reference.out" "$work/minic.out"
printf '%s\n' 'PASS compiler/c0/core-integer-equality'
