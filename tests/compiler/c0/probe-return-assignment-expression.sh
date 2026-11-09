#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
source="$root/tests/compiler/c0/return_assignment_expression.c"

"$host_cc" -std=gnu11 -Wall -Wextra -Werror -E -P "$source" -o "$work/valid.i"
"$host_cc" -std=gnu11 -Wall -Wextra -Werror -no-pie "$source" -o "$work/gnu"
for target in x86_64-linux riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
    "$minic_cc" --target="$target" -S "$work/valid.i" -o "$work/$target.s"
    test -s "$work/$target.s"
done
"$host_cc" -no-pie -x assembler "$work/x86_64-linux.s" -o "$work/minic"
"$work/gnu"
"$work/minic"

"$host_cc" -std=gnu11 -E -P -DMINIC_INVALID_RETURN_ASSIGNMENT "$source" \
    -o "$work/invalid.i"
if "$host_cc" -std=gnu11 -Werror -fsyntax-only -x c "$work/invalid.i" \
    >"$work/gnu.err" 2>&1; then
    echo 'FAIL GNU accepted a const return-assignment target' >&2
    exit 1
fi
if "$minic_cc" --target=x86_64-linux -S "$work/invalid.i" -o "$work/invalid.s" \
    >"$work/minic.err" 2>&1; then
    echo 'FAIL MiniC accepted a const return-assignment target' >&2
    exit 1
fi

echo 'C8_RETURN_ASSIGNMENT=PASS five_target_S GNU_MiniC_x86_native=0 nested_chain_conditional=1 const_target=REJECT'
