#!/bin/sh
set -eu
: "${MINIC:?MINIC must point to the compiler binary}"
: "${RISCV_CC:=riscv64-linux-gnu-gcc}"
: "${QEMU_RISCV64:=qemu-riscv64}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work="${BUILD_DIR:-$root/build/core-logical-and-value}"
source_file="$root/tests/compiler/c0/core_logical_and_value.c"
runtime_file="$root/tests/compiler/c0/core_logical_and_value_runtime.c"
mkdir -p "$work"
cc -E -P -std=gnu11 "$source_file" -o "$work/input.i"
MINIC_CORE_IR=strict "$MINIC" -S "$work/input.i" -o "$work/strict.s"
MINIC_CORE_CODEGEN=basic-v0 "$MINIC" -S "$work/input.i" -o "$work/core.s"
for symbol in core_m19_plain core_m19_short_false core_m19_short_true \
              core_m19_get_rhs_calls core_m19_nested core_m19_cfg_statement_rhs \
              core_m19_cfg_initializer core_m19_equality_cfg_rhs core_m19_list_empty_careful_shape; do
    grep -q "^${symbol}:" "$work/core.s"
done
"$RISCV_CC" -static -O2 "$source_file" "$runtime_file" -o "$work/reference-rv64"
"$RISCV_CC" -static -O2 "$runtime_file" "$work/core.s" -o "$work/minic-rv64"
"$QEMU_RISCV64" "$work/reference-rv64" >"$work/reference.out"
"$QEMU_RISCV64" "$work/minic-rv64" >"$work/minic.out"
cmp "$work/reference.out" "$work/minic.out"
grep -F 'plain=0,0,1 nested=1,0' "$work/minic.out" >/dev/null
grep -F 'short=0/0,1/1' "$work/minic.out" >/dev/null
grep -F 'cfg=0,1,1' "$work/minic.out" >/dev/null
grep -F 'init=1,7' "$work/minic.out" >/dev/null
grep -F 'list=1,0' "$work/minic.out" >/dev/null
printf '%s\n' 'PASS compiler/c0/core-logical-and-value'
