#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cc=${HOST_CC:-cc}
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
"$cc" -E -P -std=gnu11 \
    "$root/tests/compiler/c0/riscv_extended_zero_output_implicit_volatile.c" \
    -o "$work/tlb.i"
for target in riscv32 riscv64; do
    "$minic" --target="$target-linux" -S "$work/tlb.i" -o "$work/$target.s"
    test "$(grep -c 'sfence.vma x0,' "$work/$target.s")" -eq 2
    echo "RISCV_EXTENDED_ZERO_OUTPUT_VOLATILE=PASS target=$target implicit=1 explicit=1 source=GNU_E"
done
