#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
"$host" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_builtin_popcount.c" \
    -o "$work/popcount.i"
for target in x86_64 aarch64 loongarch64 riscv64 riscv32; do
    "$minic" --target="$target-linux" -S "$work/popcount.i" -o "$work/$target.s"
    test -s "$work/$target.s"
done
"$host" -std=gnu11 -Wall -Wextra -Werror -c "$work/popcount.i" -o "$work/gnu.o"
"$host" -std=gnu11 -Wall -Wextra -Werror -no-pie \
    "$work/x86_64.s" "$root/tests/compiler/c0/gnu_builtin_popcount_driver.c" \
    -o "$work/popcount-native"
"$work/popcount-native"
printf '%s\n' 'PASS compiler/c0/gnu_builtin_popcount 5-isa=5 consteval=5 native=300000 differential-cases'
