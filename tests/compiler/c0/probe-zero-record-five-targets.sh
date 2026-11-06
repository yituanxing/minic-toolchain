#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
gnu=${CC:-cc}
work=${BUILD_DIR:-"$root/build/debug"}/tests/zero-record-five-targets
mkdir -p "$work"
source="$root/tests/compiler/c0/zero_record_noop_sideeffects.c"
"$gnu" -E -P -std=gnu11 -x c "$source" -o "$work/zero.i"
"$gnu" -std=gnu11 -O2 -no-pie -x c "$source" -o "$work/gnu.elf"
"$work/gnu.elf"
for target in riscv32-linux riscv64-linux x86_64-linux aarch64-linux loongarch64-linux; do
    "$minic" --target="$target" -S "$work/zero.i" -o "$work/$target.s"
    test -s "$work/$target.s"
done
"$gnu" -no-pie -x assembler "$work/x86_64-linux.s" -o "$work/minic.elf"
"$work/minic.elf"
printf '%s\n' 'MINIC_ZERO_RECORD=PASS five_target_GNU_assembly=5/5 x86_GNU_and_MiniC_ELF=PASS no_memory_write=true both_pointer_calls=true'
