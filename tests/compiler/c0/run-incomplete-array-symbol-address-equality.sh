#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-incomplete-array-symbol-address-equality

mkdir -p "$work"
"$host_cc" -std=gnu11 -Wall -Wextra -Werror -fsyntax-only \
    "$root/tests/compiler/c0/incomplete_array_symbol_address_equality.c"
"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/incomplete_array_symbol_address_equality.c" -o "$work/good.i"

for target in x86_64-linux riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
    MINIC_CORE_IR=strict "$minic_cc" --target="$target" -S \
        "$work/good.i" -o "$work/$target.s"
done

for stem in invalid_array_symbol_equality_element invalid_array_symbol_equality_extent \
            invalid_array_symbol_equality_nested_const; do
    if "$host_cc" -std=gnu11 -Wall -Wextra -Werror -fsyntax-only \
        "$root/tests/compiler/c0/$stem.c" \
        >"$work/$stem.gnu.stdout" 2>"$work/$stem.gnu.stderr"; then
        echo "FAIL GCC accepted $stem" >&2
        exit 1
    fi
    "$host_cc" -E -P -x c "$root/tests/compiler/c0/$stem.c" -o "$work/$stem.i"
    if "$minic_cc" --target=x86_64-linux -S "$work/$stem.i" -o "$work/$stem.s" \
        >"$work/$stem.stdout" 2>"$work/$stem.stderr"; then
        echo "FAIL MiniC accepted $stem" >&2
        exit 1
    fi
    grep -F 'binary operator requires int operands' "$work/$stem.stderr" >/dev/null
done

printf '%s\n' 'PASS compiler/c0/incomplete-array-symbol-address-equality GNU-differential=1 incomplete-pair=1 complete-incomplete=1 const-element-compatible=1 bad-element=rejected bad-extent=rejected bad-nested-const=rejected five-targets=1 strict-core=1'
