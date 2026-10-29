#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

positive="$root/tests/compiler/c0/gnu_static_null_pointer_conditional.c"
negative="$root/tests/compiler/c0/gnu_static_null_pointer_conditional_negative.c"
"$host_cc" -E -P -std=gnu11 -x c "$positive" -o "$work/positive.i"
"$host_cc" -E -P -std=gnu11 -x c "$negative" -o "$work/negative.i"
"$host_cc" -O2 -std=gnu11 -no-pie "$positive" -o "$work/gnu"
"$work/gnu"
if "$host_cc" -std=gnu11 -c "$negative" -o "$work/negative.gnu.o" \
    >"$work/gnu-negative.stdout" 2>"$work/gnu-negative.stderr"; then
    printf '%s\n' 'FAIL GNU accepted pointer load as static integer constant' >&2
    exit 1
fi

for target in x86_64 aarch64 loongarch64 riscv64 riscv32; do
    "$minic" --target="$target-linux" -S "$work/positive.i" \
        -o "$work/$target.s"
    test -s "$work/$target.s"
    if "$minic" --target="$target-linux" -S "$work/negative.i" \
        -o "$work/$target-negative.s" >"$work/minic-negative.stdout" \
        2>"$work/minic-negative.stderr"; then
        printf '%s\n' "FAIL MiniC $target accepted runtime pointer as static constant" >&2
        exit 1
    fi
done

"$host_cc" -no-pie -x assembler "$work/x86_64.s" -o "$work/minic"
"$work/minic"

printf '%s\n' 'PASS compiler/c0/gnu_static_pointer_condition five_isa=5 gnu_native=1 x86_native=1 negative_gnu=1 negative_5isa=5'
