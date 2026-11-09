#!/bin/sh
# MC-036: a record-valued ?: must fork/meet straight-line local facts.
set -eu

: "${MINIC_CC:?set MINIC_CC to the compiler under test}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
host_cc=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
source_file="$root/tests/compiler/c0/mc036_record_conditional_fact_join.c"

"$host_cc" -std=gnu11 -Wall -Wextra -Werror -O0 "$source_file" \
    -o "$work/gnu"
"$host_cc" -std=gnu11 -E -P "$source_file" -o "$work/source.i"
for target in x86_64-linux riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
    "$MINIC_CC" --target="$target" -S "$work/source.i" \
        -o "$work/$target.s"
    test -s "$work/$target.s"
done
"$host_cc" -no-pie "$work/x86_64-linux.s" -o "$work/minic"
"$work/gnu"
"$work/minic"
echo 'MC036_RECORD_CONDITIONAL_FACTS=PASS paths=10 five_target_S=5 GNU_MiniC_x86_native=0'
