#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

source_file="$root/tests/compiler/c0/gnu_nested_union_compound_record_array.c"
"$host_cc" -E -P -std=gnu11 -x c "$source_file" -o "$work/input.i"
"$host_cc" -O2 -std=gnu11 -no-pie "$source_file" -o "$work/gnu"
"$work/gnu"

for target in x86_64 aarch64 loongarch64 riscv64 riscv32; do
    "$minic" --target="$target-linux" -S "$work/input.i" \
        -o "$work/$target.s"
    test -s "$work/$target.s"
done

"$host_cc" -no-pie -x assembler "$work/x86_64.s" -o "$work/minic" \
    2>"$work/assembler.stderr"
"$work/minic"

printf '%s\n' 'PASS compiler/c0/gnu_nested_union_compound_record_array five_isa=5 gnu_native=1 x86_native=1 composite_union_record=1 reloc=4'
