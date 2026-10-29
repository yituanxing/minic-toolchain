#!/usr/bin/env bash
set -Eeuo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-${1:-"$root/build/release/bin/minic-cc"}}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

positive="$root/tests/compiler/c0/gnu_global_array_static_provenance.c"
cc -std=gnu11 -E -P "$positive" -o "$work/static-global.i"

for target in x86_64-linux aarch64-linux loongarch64-linux riscv32-linux riscv64-linux; do
    "$minic" --target="$target" -S "$work/static-global.i" -o "$work/$target.s"
    test -s "$work/$target.s"
done

cc -std=gnu11 -O0 -no-pie "$positive" -o "$work/gnu-native"
cc -O0 -no-pie "$work/x86_64-linux.s" -o "$work/minic-native"
"$work/gnu-native"
"$work/minic-native"

echo 'MINIC_GNU_GLOBAL_ARRAY_STATIC_PROVENANCE=PASS five_isa=5 literal_zero=5 global_zero=5 dynamic_index=5 gnu_native=1 minic_native=1'
