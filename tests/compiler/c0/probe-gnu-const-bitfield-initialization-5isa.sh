#!/usr/bin/env bash
set -Eeuo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-${1:-"$root/build/release/bin/minic-cc"}}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

positive="$root/tests/compiler/c0/gnu_const_record_compound_call.c"
negative="$root/tests/compiler/c0/gnu_const_bitfield_after_initialization_invalid.c"

# The C initializer of const struct { bitfields } is not a later mutable write.
# Every target must materialize the same compound object for a by-value call.
for target in x86_64-linux aarch64-linux loongarch64-linux riscv32-linux riscv64-linux; do
    "$minic" --target="$target" -S "$positive" -o "$work/$target.s"
    if "$minic" --target="$target" -S "$negative" \
        -o "$work/$target-rejected.s" >"$work/$target.stdout" 2>"$work/$target.stderr"; then
        echo "const bit-field mutation accepted on $target" >&2
        exit 1
    fi
    grep -F 'requires a modifiable object lvalue' "$work/$target.stderr" >/dev/null
    test ! -s "$work/$target-rejected.s"
done

cc -std=gnu11 -O0 -no-pie "$positive" -o "$work/gnu-native"
cc -O0 -no-pie "$work/x86_64-linux.s" -o "$work/minic-native"
"$work/gnu-native"
"$work/minic-native"

# Optional Linux 6.6.143 preprocessed i915 full-file models for CI owners.
if test -n "${MINIC_I915_MODEL_DIR:-}"; then
    for name in intel-ddi intel_display_power_well intel_dpll_mgr; do
        "$minic" --target=x86_64-linux -fshort-wchar -funsigned-char -fno-PIE \
            -S "$MINIC_I915_MODEL_DIR/minic-i915-$name.i" \
            -o "$work/$name.s"
    done
fi

echo 'MINIC_GNU_CONST_BITFIELD_INITIALIZATION=PASS five_isa=5 reject_late_const_mutation=5 x86_gnu_native=1 x86_minic_native=1'
