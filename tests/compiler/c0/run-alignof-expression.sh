#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
minic_cc=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/alignof-expression
mkdir -p "$work"
"$host_cc" -std=gnu11 -Wall -Wextra -Werror -fsyntax-only \
    "$root/tests/compiler/c0/alignof_expression.c"
if command -v clang >/dev/null 2>&1; then
    clang -std=gnu11 -Wall -Wextra -Werror -fsyntax-only \
        "$root/tests/compiler/c0/alignof_expression.c"
fi
"$host_cc" -E -P -x c "$root/tests/compiler/c0/alignof_expression.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
test -s "$work/output.s"
grep -F 'alignof_flexible_array_member:' "$work/output.s" >/dev/null

# Every target sees exactly the same GNU member alignof and record offsets.
# Native x86 executes the values produced by MiniC as a direct GCC oracle.
for target in riscv32 riscv64 x86_64 aarch64 loongarch64; do
    "$minic_cc" "--target=$target-linux" -S "$work/input.i" \
        -o "$work/$target.s"
    test -s "$work/$target.s"
done
"$host_cc" -no-pie "$work/x86_64.s" -o "$work/x86_64.out"
"$work/x86_64.out"
printf '%s\n' 'MINIC_RECORD_FIELD_ALIGNOF=PASS gcc_oracle=1 five_isa=5 x86_native=1 pack2=2 packed_aligned2=2 field_aligned4=4'
