#!/bin/sh
# Static record-field arrays use the same initializer/relocation slots whether
# the field is a legacy declarator dimension or a materialized typedef array.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-${CC:-gcc}}
positive="$root/tests/compiler/c0/static_field_typedef_array.c"
designated_zero="$root/tests/compiler/c0/static_field_zero_designated.c"
negative="$root/tests/compiler/c0/static_field_typedef_array_negative.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

test -x "$minic"
# Backward designators and intentional rewrites are accepted GNU C.  Only
# silence -Woverride-init, not genuine type or bound diagnostics.
"$host_cc" -std=gnu11 -Wall -Wextra -Werror -Wno-override-init \
    -fsyntax-only "$positive"
"$host_cc" -E -P -std=gnu11 "$positive" -o "$work/positive.i"
"$host_cc" -std=gnu11 -Wall -Wextra -Werror -fsyntax-only "$designated_zero"
"$host_cc" -E -P -std=gnu11 "$designated_zero" -o "$work/designated-zero.i"

targets='riscv32-linux riscv64-linux x86_64-linux aarch64-linux loongarch64-linux'
for target in $targets; do
    "$minic" --target="$target" -S "$work/positive.i" -o "$work/$target.s"
    test -s "$work/$target.s"
    "$minic" --target="$target" -S "$work/designated-zero.i" \
        -o "$work/$target.designated-zero.s"
    test -s "$work/$target.designated-zero.s"
done

"$host_cc" -std=gnu11 -Wno-override-init -O0 -fno-pie -no-pie \
    "$positive" -o "$work/gnu-native"
"$host_cc" -fno-pie -no-pie -x assembler "$work/x86_64-linux.s" \
    -o "$work/minic-native"
for variant in gnu-native minic-native; do
    set +e
    "$work/$variant" >"$work/$variant.stdout" 2>"$work/$variant.stderr"
    rc=$?
    set -e
    if test "$rc" -ne 42; then
        printf '%s\n' "STATIC_FIELD_TYPEDEF_ARRAY=FAIL native=$variant exit=$rc expected=42" >&2
        exit 1
    fi
done
cmp "$work/gnu-native.stdout" "$work/minic-native.stdout"
cmp "$work/gnu-native.stderr" "$work/minic-native.stderr"

# A GNU zero-length record field also supports a designated empty initializer
# in the current MiniC parser.  This is a positive, not an invalid GNU case.
"$host_cc" -std=gnu11 -O0 -fno-pie -no-pie "$designated_zero" \
    -o "$work/gnu-zero-native"
"$host_cc" -fno-pie -no-pie -x assembler \
    "$work/x86_64-linux.designated-zero.s" -o "$work/minic-zero-native"
for variant in gnu-zero-native minic-zero-native; do
    set +e
    "$work/$variant" >"$work/$variant.stdout" 2>"$work/$variant.stderr"
    rc=$?
    set -e
    if test "$rc" -ne 42; then
        printf '%s\n' "STATIC_FIELD_TYPEDEF_ARRAY=FAIL native=$variant exit=$rc expected=42" >&2
        exit 1
    fi
done
cmp "$work/gnu-zero-native.stdout" "$work/minic-zero-native.stdout"
cmp "$work/gnu-zero-native.stderr" "$work/minic-zero-native.stderr"

if test -n "${MINIC_BASELINE_CC:-}"; then
    test -x "$MINIC_BASELINE_CC"
    for target in $targets; do
        "$MINIC_BASELINE_CC" --target="$target" -S "$work/positive.i" \
            -o "$work/$target.baseline.s"
        cmp "$work/$target.baseline.s" "$work/$target.s"
        "$MINIC_BASELINE_CC" --target="$target" -S "$work/designated-zero.i" \
            -o "$work/$target.designated-zero.baseline.s"
        cmp "$work/$target.designated-zero.baseline.s" \
            "$work/$target.designated-zero.s"
    done
fi

for case_no in 1 2 3 4 5 6 7 8; do
    if "$host_cc" -std=gnu11 -Wall -Wextra -Werror -fsyntax-only \
        -DCASE="$case_no" "$negative" >"$work/gnu-$case_no.log" 2>&1; then
        printf '%s\n' "STATIC_FIELD_TYPEDEF_ARRAY=FAIL GNU_ACCEPTED_CASE=$case_no" >&2
        exit 1
    fi
    "$host_cc" -E -P -std=gnu11 -DCASE="$case_no" "$negative" \
        -o "$work/negative-$case_no.i"
    for target in $targets; do
        output="$work/negative-$case_no-$target.s"
        if "$minic" --target="$target" -S "$work/negative-$case_no.i" \
            -o "$output" >"$work/minic-$case_no-$target.log" 2>&1; then
            printf '%s\n' "STATIC_FIELD_TYPEDEF_ARRAY=FAIL MINIC_ACCEPTED_CASE=$case_no target=$target" >&2
            exit 1
        fi
        if test -e "$output"; then
            printf '%s\n' "STATIC_FIELD_TYPEDEF_ARRAY=FAIL PARTIAL_OUTPUT_CASE=$case_no target=$target" >&2
            exit 1
        fi
    done
done

printf '%s\n' 'STATIC_FIELD_TYPEDEF_ARRAY=PASS targets=5 gnu_native=42 minic_x86_native=42 typedef_field=1 designated=1 backward=1 overwrite=1 union_overlay=1 relocation=1 gnu_zero_positional=1 gnu_zero_designated=1 negative=40 rollback=40'
