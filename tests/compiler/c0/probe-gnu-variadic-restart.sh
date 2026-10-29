#!/usr/bin/env bash
set -Eeuo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-${1:-"$root/build/release/bin/minic-cc"}}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
source="$root/tests/compiler/c0/gnu_variadic_restart_aapcs64.c"
driver="$root/tests/compiler/c0/gnu_variadic_restart_aapcs64_driver.c"
invalid="$root/tests/compiler/c0/gnu_variadic_restart_invalid.c"

for target in x86_64-linux aarch64-linux riscv32-linux riscv64-linux loongarch64-linux; do
    "$minic" --target="$target" -S "$source" -o "$work/$target.s"
    if "$minic" --target="$target" -S "$invalid" \
        -o "$work/$target-bad.s" >"$work/$target.out" 2>"$work/$target.err"; then
        echo "invalid nonvariadic va_start accepted on $target" >&2
        exit 1
    fi
    test ! -s "$work/$target-bad.s"
done

# Distinct AAPCS64 tags are allocated per source va_start. The register-save
# bank itself is immutable and shared. The second restart is not an alias of
# the first cursor's mutable GP/FP offsets.
awk '
    /^  movz x9, #[0-9]+$/ { cursor = $3 }
    /^  str x10, \[x9\]$/ { slots[++count] = cursor }
    END { if (count != 2 || slots[1] == slots[2]) exit 1 }
' "$work/aarch64-linux.s"

cc -O0 -no-pie "$source" "$driver" -o "$work/gnu-native"
cc -O0 -no-pie "$work/x86_64-linux.s" "$driver" -o "$work/minic-native"
"$work/gnu-native"
"$work/minic-native"

echo 'MINIC_GNU_VA_RESTART=PASS five_isa=5 AAPCS64_independent_cursors=2 GNU_native=1 MiniC_x86_native=1 nonvariadic_rejected=5'
