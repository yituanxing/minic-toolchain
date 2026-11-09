#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

"$host_cc" -std=gnu11 -O2 -Wno-error=incompatible-pointer-types \
    "$root/tests/compiler/c0/gnu_enum_underlying_pointer.c" -o "$work/gnu" \
    2>"$work/gnu-warnings.log"
"$work/gnu"
"$host_cc" -std=gnu11 -E -P -x c \
    "$root/tests/compiler/c0/gnu_enum_underlying_pointer.c" -o "$work/positive.i"

for target in x86_64 aarch64 loongarch64 riscv64 riscv32; do
    "$minic" --target="$target-linux" -S "$work/positive.i" -o "$work/$target.s"
    test -s "$work/$target.s"
done
for invalid in invalid_gnu_enum_underlying_pointer invalid_gnu_enum_qualifier_drop; do
    "$host_cc" -std=gnu11 -E -P -x c \
        "$root/tests/compiler/c0/$invalid.c" -o "$work/$invalid.i"
    for target in x86_64 aarch64 loongarch64 riscv64 riscv32; do
        if "$minic" --target="$target-linux" -S "$work/$invalid.i" \
            -o "$work/$invalid-$target.s" >"$work/bad.out" 2>"$work/bad.err"; then
            printf 'FAIL accepted invalid enum-pointer target=%s source=%s\n' "$target" "$invalid" >&2
            exit 1
        fi
    done
done
printf '%s\n' 'PASS compiler/c0/gnu_enum_underlying_pointer GNU native=1 five-isa=5 strict-negatives=10'
