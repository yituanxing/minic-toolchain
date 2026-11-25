#!/usr/bin/env bash
set -Eeuo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-${1:-"$root/build/release/bin/minic-cc"}}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

positive="$root/tests/compiler/c0/gnu_indirect_record_call_abi.c"
unsupported_fp="$root/tests/compiler/c0/gnu_indirect_fp_record_abi_negative.c"

# Cover 1-GP, 2-GP, and hidden sret aggregate returns through a first-class
# function pointer, with direct aggregate by-value consumers in the same TU.
for target in x86_64-linux aarch64-linux loongarch64-linux riscv32-linux riscv64-linux; do
    "$minic" --target="$target" -S "$positive" -o "$work/$target.s"
    test -s "$work/$target.s"
done

cc -std=gnu11 -O0 -no-pie "$positive" -o "$work/gnu-native"
cc -O0 -no-pie "$work/x86_64-linux.s" -o "$work/minic-native"
"$work/gnu-native"
"$work/minic-native"

# SysV SSE-class aggregate returns require an FP register result classifier.
# They cannot be silently materialized from a GP register as an approximation.
if "$minic" --target=x86_64-linux -S "$unsupported_fp" \
    -o "$work/unsupported-fp.s" >"$work/unsupported-fp.out" \
    2>"$work/unsupported-fp.err"; then
    echo 'unsupported SysV aggregate FP return unexpectedly accepted' >&2
    exit 1
fi
grep -F 'x86-64 Core instruction unsupported' "$work/unsupported-fp.err" >/dev/null
test ! -s "$work/unsupported-fp.s"

if test -n "${MINIC_I915_MODEL_DIR:-}"; then
    for name in intel_dp_aux intel_gt_sysfs_pm; do
        "$minic" --target=x86_64-linux -fshort-wchar -funsigned-char -fno-PIE \
            -S "$MINIC_I915_MODEL_DIR/minic-i915-$name.i" \
            -o "$work/$name.s"
    done
fi

echo 'MINIC_GNU_INDIRECT_RECORD_RETURN=PASS five_isa=5 register1=5 register2=5 sret=5 x86_gnu_native=1 x86_minic_native=1 rejected_sysv_fp_record=1'
