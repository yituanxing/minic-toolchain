#!/usr/bin/env bash
set -Eeuo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-${1:-"$root/build/release/bin/minic-cc"}}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

positive="$root/tests/compiler/c0/gnu_variadic_single_scalar_record.c"
negative="$root/tests/compiler/c0/gnu_variadic_multifield_record_negative.c"
driver="$root/tests/compiler/c0/gnu_variadic_single_scalar_record_driver.c"

for target in x86_64-linux aarch64-linux riscv32-linux riscv64-linux loongarch64-linux; do
    "$minic" --target="$target" -S "$positive" -o "$work/$target.s"
done

# GCC and MiniC must agree on both the register-save area and overflow-stack
# paths for one-field integer/FP/pointer aggregates.
cc -O0 -no-pie "$positive" "$driver" -o "$work/gnu-native"
cc -O0 -no-pie "$work/x86_64-linux.s" "$driver" -o "$work/minic-native"
"$work/gnu-native"
"$work/minic-native"

# Generic linear-slot ABIs already support raw record copying. On SysV and
# AAPCS64, multi-field records require an aggregate ABI classifier: rejecting
# them is intentional until that classifier is present.
for target in x86_64-linux aarch64-linux; do
    if "$minic" --target="$target" -S "$negative" \
        -o "$work/$target-negative.s" >"$work/$target.out" 2>"$work/$target.err"; then
        echo "unsupported aggregate va_arg accepted on $target" >&2
        exit 1
    fi
    grep -F "Core IR does not yet support function" "$work/$target.err" >/dev/null
    test ! -s "$work/$target-negative.s"
done
for target in riscv32-linux riscv64-linux loongarch64-linux; do
    "$minic" --target="$target" -S "$negative" -o "$work/$target-negative.s"
done

if test -n "${MINIC_P9_LINUX_MODEL_I:-}"; then
    "$minic" --target=x86_64-linux -S "$MINIC_P9_LINUX_MODEL_I" \
        -o "$work/linux-p9-model.s"
    grep -F 'p9pdu_vwritef:' "$work/linux-p9-model.s" >/dev/null
fi
echo 'MINIC_GNU_VA_RECORD=PASS five_isa=5 x86_gnu_native=1 x86_minic_native=1 gp_fp_overflow=1 pointer=1 ABI_CURSOR_multi_field_rejected=2 LINEAR_SLOT_preserved=3'
