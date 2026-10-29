#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

"${HOST_CC:-cc}" -std=gnu11 -Wall -Wextra -Werror -c \
    "$root/tests/compiler/c0/gnu_qualified_array_alias.c" -o "$work/gnu.o"
"${HOST_CC:-cc}" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/gnu_qualified_array_alias.c" -o "$work/input.i"

for target in x86_64 aarch64 loongarch64 riscv64 riscv32; do
    "$minic" --target="$target-linux" -S \
        "$work/input.i" \
        -o "$work/$target.s"
    test -s "$work/$target.s"
    grep -Fq '.set crypto_aes_sbox, aes_sbox' "$work/$target.s"
    grep -Fq '.set crypto_aes_inv_sbox, aes_inv_sbox' "$work/$target.s"
done

printf '%s\n' 'PASS compiler/c0/gnu_qualified_array_alias five-isa=5 actual-storage-alias=2 gnu=1'
