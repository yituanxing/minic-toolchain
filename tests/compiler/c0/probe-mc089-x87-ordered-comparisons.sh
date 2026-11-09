#!/bin/sh
# MC-089: x87 sign-extended comparison-helper result, not literal rounding.
set -eu

: "${MINIC_CC:?MINIC_CC is required}"
host_cc=${HOST_CC:-cc}
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
source_file="$root/tests/compiler/audit/mc089_x87_ordered_comparisons.c"
support_file="$root/tests/compiler/audit/mc089_x87_ordered_comparisons_support.c"
work=$(mktemp -d)
trap 'rm -r -- "$work"' EXIT HUP INT TERM

"$host_cc" -std=gnu11 -E -P "$source_file" -o "$work/input.i"
"$host_cc" -std=gnu11 -Wall -Wextra -Werror -O0 -no-pie \
    "$source_file" "$support_file" -o "$work/gnu"
"$MINIC_CC" --target=x86_64-linux -S "$work/input.i" -o "$work/mini.s"
"$host_cc" -no-pie "$work/mini.s" "$support_file" -o "$work/mini"
"$work/gnu" > "$work/gnu.out"
"$work/mini" > "$work/mini.out"
cmp "$work/gnu.out" "$work/mini.out"
grep -Fx 'MC089_MISMATCHES=0' "$work/mini.out" >/dev/null

# RV32 has an unrelated existing long-double Core frame limitation. Check
# the x87 fix and the three 64-bit binary128 backends; no RV32 pass claim.
for target in x86_64-linux riscv64-linux aarch64-linux loongarch64-linux; do
    "$MINIC_CC" --target="$target" -S "$work/input.i" \
        -o "$work/$target.s"
done

printf '%s\n' 'MC089_X87_ORDERED_CMP=PASS all_six_operators_nan_zero_infinity_12_cases GNU_MiniC_x86_native direct_2gt1 four_target_S'
