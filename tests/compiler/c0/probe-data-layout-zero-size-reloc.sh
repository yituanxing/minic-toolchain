#!/usr/bin/env bash
set -Eeuo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

# The normal global emitter may use its own recursive initializer walker.
# This ABI-independent unit test explicitly exercises both paths inside the
# DataLayout AGGREGATE_SCALAR relocation offset resolver under UBSan.
cc -I"$root/include" -I"$root/src" -std=c11 -O0 -g \
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes -Werror \
    -fsanitize=undefined -fno-sanitize-recover=undefined \
    "$root/src/frontend/ast.c" \
    "$root/src/frontend/type.c" \
    "$root/src/target/data_layout.c" \
    "$root/tests/target/data_layout_zero_size_reloc_test.c" \
    -o "$work/layout-ubsan"
"$work/layout-ubsan"

source="$root/tests/compiler/c0/gnu_zero_size_aggregate_reloc.c"
cc -std=gnu11 -O0 -no-pie "$source" -o "$work/gnu-native"
"$work/gnu-native"
cc -E -P -x c "$source" -o "$work/fixture.i"

for target in x86_64-linux aarch64-linux riscv32-linux riscv64-linux loongarch64-linux; do
    "$minic" --target="$target" -S "$work/fixture.i" -o "$work/$target.s"
    test -s "$work/$target.s"
done
cc -O0 -no-pie "$work/x86_64-linux.s" -o "$work/minic-native"
"$work/minic-native"

echo 'DATA_LAYOUT_ZERO_SIZE_AGGREGATE=PASS sanitizer=2 branches=2 GNU_native=1 MiniC_native=1 five_ISA_S=5'
