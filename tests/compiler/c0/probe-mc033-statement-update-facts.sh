#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
scratch_probe=$(mktemp -d)
trap 'rm -rf "$scratch_probe"' EXIT HUP INT TERM
source="$root/tests/compiler/c0/mc033_statement_updates_local_facts.c"

"$host_cc" -std=gnu11 -Wall -Wextra -Werror -E -P "$source" \
    -o "$scratch_probe/input.i"
"$host_cc" -std=gnu11 -Wall -Wextra -Werror -O0 -no-pie "$source" \
    -o "$scratch_probe/gnu"
for target in x86_64-linux riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
    "$minic_cc" --target="$target" -S "$scratch_probe/input.i" \
        -o "$scratch_probe/$target.s"
    test -s "$scratch_probe/$target.s"
done
"$host_cc" -no-pie -x assembler "$scratch_probe/x86_64-linux.s" \
    -o "$scratch_probe/minic"
"$scratch_probe/gnu"
"$scratch_probe/minic"

echo 'MC033_STATEMENT_UPDATE_FACTS=PASS four_updates nested_expression goto continue five_targets_S GNU_MiniC_x86_native=0'
