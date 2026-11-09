#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
"$host" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/gnu_builtin_overflow_predicate.c" -o "$work/overflow.i"
"$host" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/gnu_builtin_overflow_predicate_portable.c" \
    -o "$work/portable.i"
for target in x86_64 aarch64 loongarch64 riscv64 riscv32; do
    "$minic" --target="$target-linux" -S "$work/portable.i" \
        -o "$work/$target-portable.s"
    test -s "$work/$target-portable.s"
done
"$minic" --target=x86_64-linux -S "$work/overflow.i" -o "$work/x86_64.s"
test -s "$work/x86_64.s"
"$host" -std=gnu11 -Wall -Wextra -Werror -no-pie \
    "$work/x86_64.s" \
    "$root/tests/compiler/c0/gnu_builtin_overflow_predicate_driver.c" \
    -o "$work/predicate-native"
"$work/predicate-native"
printf '%s\n' 'PASS compiler/c0/gnu_builtin_overflow_predicate 5-isa=portable2 x86=full5 native=50000 constexpr=7 non-evaluation=1'
