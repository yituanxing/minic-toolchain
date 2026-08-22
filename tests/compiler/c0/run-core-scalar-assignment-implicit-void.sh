#!/bin/sh
set -eu

: "${MINIC:?MINIC must point to the compiler binary}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work="${BUILD_DIR:-$root/build/core-scalar-assignment-implicit-void}"
source_file="$root/tests/compiler/c0/core_scalar_assignment_implicit_void.c"
runtime_file="$root/tests/compiler/c0/core_scalar_assignment_implicit_void_runtime.c"
mkdir -p "$work"

cc -E -P -std=gnu11 "$source_file" -o "$work/core_scalar_assignment_implicit_void.i"
MINIC_CORE_IR=strict "$MINIC" -S "$work/core_scalar_assignment_implicit_void.i" \
    -o "$work/core_scalar_assignment_implicit_void-strict.s"
MINIC_CORE_CODEGEN=basic-v0 "$MINIC" -S "$work/core_scalar_assignment_implicit_void.i" \
    -o "$work/core_scalar_assignment_implicit_void-core.s"

for symbol in core_m4_init_list_head core_m4_pointer_store core_m4_empty_void; do
    grep -q "^${symbol}:" "$work/core_scalar_assignment_implicit_void-core.s"
    grep -q "${symbol}_core_bb0" "$work/core_scalar_assignment_implicit_void-core.s"
done

"$RISCV_CC" -static -O2 "$source_file" "$runtime_file" -o "$work/reference-rv64"
"$RISCV_CC" -static -O2 "$runtime_file" "$work/core_scalar_assignment_implicit_void-core.s" \
    -o "$work/minic-rv64"
"$QEMU_RISCV64" "$work/reference-rv64" >"$work/reference.out"
"$QEMU_RISCV64" "$work/minic-rv64" >"$work/minic.out"
cmp "$work/reference.out" "$work/minic.out"
printf '%s\n' 'PASS compiler/c0/core-scalar-assignment-implicit-void'
