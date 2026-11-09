#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-is-constexpr-bitfield
fixture="$root/tests/compiler/c0/gnu_is_constexpr_bitfield.c"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 "$fixture" -o "$work/valid.i"
"$host_cc" -std=gnu11 -fsyntax-only "$fixture"
for target in riscv32-linux riscv64-linux x86_64-linux aarch64-linux loongarch64-linux; do
    "$minic" --target="$target" -S "$work/valid.i" -o "$work/valid-$target.s"
    grep -F 'kernel_i915_bitfield_probe:' "$work/valid-$target.s" >/dev/null
done

for invalid in REJECT_ZERO_MASK REJECT_OUT_OF_RANGE_VALUE; do
    "$host_cc" -E -P -std=gnu11 "-D$invalid" "$fixture" -o "$work/$invalid.i"
    if "$host_cc" -std=gnu11 -fsyntax-only "-D$invalid" "$fixture" \
        >"$work/$invalid.gnu.out" 2>"$work/$invalid.gnu.err"; then
        printf '%s\n' "FAIL GNU reference accepted $invalid" >&2
        exit 1
    fi
    for target in riscv32-linux riscv64-linux x86_64-linux aarch64-linux loongarch64-linux; do
        if "$minic" --target="$target" -S "$work/$invalid.i" \
            -o "$work/$invalid-$target.s" >"$work/$invalid-$target.out" \
            2>"$work/$invalid-$target.err"; then
            printf '%s\n' "FAIL $target accepted $invalid invalid register bitfield" >&2
            exit 1
        fi
        grep -F 'bit-field width exceeds its integer type' \
            "$work/$invalid-$target.err" >/dev/null
    done
done

printf '%s\n' 'PASS compiler/c0/gnu_is_constexpr_bitfield five_targets=5 integer_ICE_null=1 runtime_zero_not_ICE=1 GNU_reference=1 invalid_negative_width=10'
