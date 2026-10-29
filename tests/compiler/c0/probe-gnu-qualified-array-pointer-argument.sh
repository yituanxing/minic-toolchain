#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

"$host_cc" -std=gnu11 -Wall -Wextra -Werror \
    "$root/tests/compiler/c0/gnu_qualified_array_pointer_argument.c" \
    -o "$work/gnu"
"$work/gnu"
"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/gnu_qualified_array_pointer_argument.c" \
    -o "$work/positive.i"
for target in x86_64 aarch64 loongarch64 riscv64 riscv32; do
    "$minic" --target="$target-linux" -S "$work/positive.i" -o "$work/$target.s"
    test -s "$work/$target.s"
done

"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/invalid_gnu_qualified_array_pointer_argument.c" \
    -o "$work/invalid.i"
if "$host_cc" -std=gnu11 -Wall -Wextra -Werror -fsyntax-only "$work/invalid.i" \
    >"$work/gnu-bad.stdout" 2>"$work/gnu-bad.stderr"; then
    printf '%s\n' 'FAIL GCC -Werror accepted const array pointer drop' >&2
    exit 1
fi
for target in x86_64 aarch64 loongarch64 riscv64 riscv32; do
    if "$minic" --target="$target-linux" -S "$work/invalid.i" \
        -o "$work/invalid-$target.s" >"$work/minic-bad.out" 2>"$work/minic-bad.err"; then
        printf 'FAIL MiniC accepted const array pointer drop target=%s\n' "$target" >&2
        exit 1
    fi
done
printf '%s\n' 'PASS compiler/c0/gnu_qualified_array_pointer_argument GNU native=1 five-isa=5 const-drop-reject=5'
