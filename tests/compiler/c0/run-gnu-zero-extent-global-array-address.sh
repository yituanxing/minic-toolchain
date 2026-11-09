#!/usr/bin/env bash
set -Eeuo pipefail

minic=${1:?expected MiniC-cc executable}
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

positive="$root/tests/compiler/c0/gnu_zero_extent_global_array_address.c"
negative="$root/tests/compiler/c0/invalid_incomplete_global_array_sizeof.c"

gcc -E -P -x c "$positive" -o "$work/positive.i"
gcc -std=gnu11 -Wall -Wextra -Werror -O0 -no-pie "$positive" -o "$work/gnu-native"
"$work/gnu-native"

for target in x86_64-linux aarch64-linux loongarch64-linux riscv32-linux riscv64-linux; do
    "$minic" --target="$target" -S "$work/positive.i" -o "$work/$target.s"
    test -s "$work/$target.s"
done
gcc -no-pie -x assembler "$work/x86_64-linux.s" \
    -o "$work/minic-native" 2>"$work/assembler.err"
"$work/minic-native"

gcc -E -P -x c "$negative" -o "$work/negative.i"
if gcc -std=gnu11 -Wall -Wextra -Werror -fsyntax-only "$negative" \
    >"$work/gnu.out" 2>"$work/gnu.err"; then
    echo 'GNU unexpectedly accepted sizeof on incomplete extern array' >&2
    exit 1
fi
for target in x86_64-linux aarch64-linux loongarch64-linux riscv32-linux riscv64-linux; do
    if "$minic" --target="$target" -S "$work/negative.i" \
        -o "$work/$target.negative.s" >"$work/$target.negative.out" \
        2>"$work/$target.negative.err"; then
        echo "MiniC $target unexpectedly accepted incomplete array sizeof" >&2
        exit 1
    fi
    test ! -s "$work/$target.negative.s"
done

echo 'MINIC_GNU_ZERO_EXTENT_GLOBAL_ARRAY_ADDRESS=PASS five_isa=5 native_gnu=1 native_minic=1 reject_gnu=1 reject_5isa=5'
