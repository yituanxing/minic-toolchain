#!/usr/bin/env bash
set -Eeuo pipefail

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-${1:-"$root/build/release/bin/minic-cc"}}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

positive_switch="$root/tests/compiler/c0/gnu_nested_switch_return_reachability.c"
positive_record="$root/tests/compiler/c0/gnu_statement_expr_record_return.c"
negative_record="$root/tests/compiler/c0/gnu_statement_expr_record_return_mismatch_negative.c"

cc -E -P -std=gnu11 "$positive_switch" -o "$work/switch.i"
cc -E -P -std=gnu11 "$positive_record" -o "$work/record.i"
cc -E -P -std=gnu11 "$negative_record" -o "$work/record-negative.i"

for target in x86_64-linux aarch64-linux loongarch64-linux riscv32-linux riscv64-linux; do
    for name in switch record; do
        "$minic" --target="$target" -S "$work/$name.i" -o "$work/$target-$name.s"
        test -s "$work/$target-$name.s"
    done
    if "$minic" --target="$target" -S "$work/record-negative.i" -o "$work/$target-record-negative.s" >"$work/negative.out" 2>"$work/negative.err"; then
        echo "incompatible record return improperly accepted for $target" >&2
        exit 1
    fi
done

for name in switch record; do
    if test "$name" = switch; then source="$positive_switch"; else source="$positive_record"; fi
    cc -std=gnu11 -O0 -no-pie "$source" -o "$work/gnu-$name"
    cc -O0 -no-pie "$work/x86_64-linux-$name.s" -o "$work/minic-$name"
    "$work/gnu-$name"
    "$work/minic-$name"
done

if test -n "${MINIC_X86_BATCH8_MODEL_DIR:-}"; then
    for name in hid-lg4ff hugetlb; do
        "$minic" --target=x86_64-linux -fshort-wchar -funsigned-char -fno-PIE \
            -S "$MINIC_X86_BATCH8_MODEL_DIR/minic-b8-$name.i" \
            -o "$work/$name.s"
        test -s "$work/$name.s"
    done
fi

echo 'MINIC_GNU_NESTED_SWITCH_RECORD_RETURN=PASS five_isa=5 semantic_positive=10 record_bytes=8+16+32 rejected_record_type_negative=5 gnu_native=2 minic_native=2'
