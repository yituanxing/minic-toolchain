#!/bin/sh
set -eu

: "${MINIC_CC:?MINIC_CC path required}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

valid="$root/tests/compiler/c0/static_array_redeclaration.c"
${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -O0 "$valid" -o "$work/gnu"
${CC:-cc} -E -P -x c "$valid" -o "$work/valid.i"
"$work/gnu"

for target in riscv32-linux riscv64-linux x86_64-linux aarch64-linux loongarch64-linux; do
    "$MINIC_CC" --target="$target" -S "$work/valid.i" -o "$work/$target.s"
done
${CC:-cc} -no-pie "$work/x86_64-linux.s" -o "$work/minic"
"$work/minic"

for name in static_array_redeclaration_bound_conflict \
            static_array_redeclaration_tentative_conflict; do
    invalid="$root/tests/compiler/c0/$name.c"
    if ${CC:-cc} -std=gnu11 -pedantic-errors -fsyntax-only "$invalid" \
            >"$work/$name.gcc.log" 2>&1; then
        echo "GCC unexpectedly accepted $name" >&2
        exit 1
    fi
    ${CC:-cc} -E -P -x c "$invalid" -o "$work/$name.i"
    for target in riscv32-linux riscv64-linux x86_64-linux aarch64-linux loongarch64-linux; do
        if "$MINIC_CC" --target="$target" -S "$work/$name.i" \
                -o "$work/$name.$target.s" >"$work/$name.$target.log" 2>&1; then
            echo "MiniC $target unexpectedly accepted $name" >&2
            exit 1
        fi
        if ! grep -F 'conflicting static scalar array' "$work/$name.$target.log" >/dev/null; then
            echo "MiniC $target rejected $name for an unrelated reason" >&2
            exit 1
        fi
    done
done

echo 'STATIC_ARRAY_REDECLARATION=PASS gcc_runtime=1 minic_x86_runtime=1 targets=5 negative_cases=2'
