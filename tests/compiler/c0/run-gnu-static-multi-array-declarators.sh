#!/usr/bin/env bash
set -Eeuo pipefail

minic=${1:?expected MiniC-cc executable}
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

positive="$root/tests/compiler/c0/gnu_static_multi_array_declarators.c"
gcc -E -P -x c "$positive" -o "$work/positive.i"
gcc -std=gnu11 -Wall -Wextra -Werror -O0 -no-pie "$positive" -o "$work/gnu-native"
"$work/gnu-native"

for target in x86_64-linux aarch64-linux loongarch64-linux riscv32-linux riscv64-linux; do
    "$minic" --target="$target" -S "$work/positive.i" -o "$work/$target.s"
    test -s "$work/$target.s"
done
gcc -no-pie -x assembler "$work/x86_64-linux.s" -o "$work/minic-native" 2>"$work/as.log"
"$work/minic-native"

for name in invalid_static_multi_array_missing_name invalid_static_multi_array_duplicate_name; do
    source="$root/tests/compiler/c0/$name.c"
    gcc -E -P -x c "$source" -o "$work/$name.i"
    if gcc -std=gnu11 -Wall -Wextra -Werror -fsyntax-only "$source" \
        >"$work/$name.gnu.out" 2>"$work/$name.gnu.err"; then
        echo "GNU unexpectedly accepted invalid multi-array declaration: $name" >&2
        exit 1
    fi
    for target in x86_64-linux aarch64-linux loongarch64-linux riscv32-linux riscv64-linux; do
        if "$minic" --target="$target" -S "$work/$name.i" \
            -o "$work/$name.$target.s" >"$work/$name.$target.out" \
            2>"$work/$name.$target.err"; then
            echo "MiniC $target accepted invalid multi-array declaration: $name" >&2
            exit 1
        fi
        test ! -s "$work/$name.$target.s"
    done
done

echo 'MINIC_GNU_STATIC_MULTI_ARRAY=PASS five_isa=5 gnu_native=1 minic_x86_native=1 invalid_5isa=10 invalid_gnu=2'
