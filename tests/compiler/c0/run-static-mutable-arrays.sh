#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
build_dir=${BUILD_DIR:-"$root/build/debug"}
work="$build_dir/tests/compiler-c0-static-mutable-arrays"
mkdir -p "$work"
asm="$work/static_mutable_arrays.s"

"$minic" -S "$root/tests/compiler/c0/static_mutable_arrays.c" -o "$asm"
grep -F '.type early_cmdline, @object' "$asm" >/dev/null
grep -F '.size early_cmdline, 2048' "$asm" >/dev/null
grep -F '.type riscv_isa, @object' "$asm" >/dev/null
grep -F '.size riscv_isa, 512' "$asm" >/dev/null
grep -F '.type aia_irq2bitpos, @object' "$asm" >/dev/null
grep -F '.size aia_irq2bitpos, 20' "$asm" >/dev/null
grep -F '.type fixed_values, @object' "$asm" >/dev/null
grep -F '.size fixed_values, 16' "$asm" >/dev/null

printf '%s\n' 'PASS compiler/c0/static-mutable-arrays fixed-zero=2 inferred-int=1 shared-init=1'
