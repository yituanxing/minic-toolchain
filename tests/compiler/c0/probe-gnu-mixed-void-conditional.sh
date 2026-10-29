#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

"$host_cc" -std=gnu11 -O2 -E -P -x c \
    "$root/tests/compiler/c0/gnu_mixed_void_conditional.c" -o "$work/positive.i"
"$host_cc" -std=gnu11 -O2 "$root/tests/compiler/c0/gnu_mixed_void_conditional.c" \
    -o "$work/gnu"
"$work/gnu"

for target in x86_64 aarch64 loongarch64 riscv64 riscv32; do
    "$minic" --target="$target-linux" -S "$work/positive.i" \
        -o "$work/$target.s"
    test -s "$work/$target.s"
done

"$host_cc" -std=gnu11 -E -P -x c \
    "$root/tests/compiler/c0/invalid_gnu_mixed_void_conditional_value.c" \
    -o "$work/invalid.i"
if "$host_cc" -std=gnu11 -fsyntax-only "$work/invalid.i" \
    >"$work/gnu-invalid.stdout" 2>"$work/gnu-invalid.stderr"; then
    printf '%s\n' 'FAIL GNU unexpectedly accepted nonvoid use of mixed-void result' >&2
    exit 1
fi
for target in x86_64 aarch64 loongarch64 riscv64 riscv32; do
    if "$minic" --target="$target-linux" -S "$work/invalid.i" \
        -o "$work/invalid-$target.s" >"$work/invalid.out" 2>"$work/invalid.err"; then
        printf 'FAIL MiniC unexpectedly accepted mixed-void value target=%s\n' "$target" >&2
        exit 1
    fi
done

printf '%s\n' 'PASS compiler/c0/gnu_mixed_void_conditional GNU runtime=1 five-isa=5 negative=5'
