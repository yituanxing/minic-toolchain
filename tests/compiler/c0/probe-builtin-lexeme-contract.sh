#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

for stem in builtin_offsetof gnu_compiletime_selection_builtins gnu_choose_expr_bitfield; do
    source="$root/tests/compiler/c0/$stem.c"
    "$host_cc" -std=gnu11 -Wall -Wextra -Werror -E -P "$source" -o "$work/$stem.i"
    "$host_cc" -std=gnu11 -Wall -Wextra -Werror -no-pie "$source" -o "$work/$stem.gnu"
    for target in x86_64-linux riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
        # This fixture has LP64-specific hard-coded _Static_assert offsets.
        if [ "$stem" = builtin_offsetof ] && [ "$target" = riscv32-linux ]; then
            continue
        fi
        "$minic_cc" --target="$target" -S "$work/$stem.i" -o "$work/$stem.$target.s"
        test -s "$work/$stem.$target.s"
    done
    "$host_cc" -no-pie -x assembler "$work/$stem.x86_64-linux.s" -o "$work/$stem.minic"
    "$work/$stem.gnu"
    "$work/$stem.minic"
done

echo 'C9_BUILTIN_LEXEME=PASS offsetof_4xLP64 generic+choose_expr_5targets GNU_MiniC_x86_native=0'
