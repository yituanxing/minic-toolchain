#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/release"}/tests/compiler-c0-static-typedef-array-redeclaration
mkdir -p "$work"

positive="$root/tests/compiler/c0/static_typedef_array_redeclaration.c"
"$host_cc" -std=gnu11 -Wall -Wextra -Werror "$positive" -o "$work/gcc"
"$work/gcc"
"$host_cc" -std=gnu11 -E -P "$positive" -o "$work/positive.i"

for target in riscv32-linux riscv64-linux x86_64-linux aarch64-linux loongarch64-linux; do
    "$minic_cc" --target="$target" -S "$work/positive.i" -o "$work/$target.s"
    test -s "$work/$target.s"
done
"$host_cc" -no-pie "$work/x86_64-linux.s" -o "$work/minic"
"$work/minic"

for stem in static_typedef_array_bound_conflict static_typedef_array_redefinition; do
    source="$root/tests/compiler/c0/$stem.c"
    if test "$stem" = static_typedef_array_redefinition; then
        expected_diagnostic='duplicate global object'
    else
        expected_diagnostic='conflicting static object definition'
    fi
    if "$host_cc" -std=gnu11 -Wall -Wextra -Werror -fsyntax-only "$source" \
        >"$work/$stem.gcc.out" 2>"$work/$stem.gcc.err"; then
        echo "FAIL: GCC accepted invalid typedef array redeclaration: $stem" >&2
        exit 1
    fi
    "$host_cc" -std=gnu11 -E -P "$source" -o "$work/$stem.i"
    for target in riscv32-linux riscv64-linux x86_64-linux aarch64-linux loongarch64-linux; do
        if "$minic_cc" --target="$target" -S "$work/$stem.i" \
            -o "$work/$target-$stem.s" >"$work/$target-$stem.out" \
            2>"$work/$target-$stem.err"; then
            echo "FAIL: MiniC accepted invalid typedef array redeclaration: $target/$stem" >&2
            exit 1
        fi
        grep -F "$expected_diagnostic" "$work/$target-$stem.err" >/dev/null
    done
done

printf '%s\n' \
    'STATIC_TYPEDEF_ARRAY_REDECLARATION=PASS gcc_runtime=1 minic_x86_runtime=1 targets=5 negatives=2'
