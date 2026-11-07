#!/bin/sh
# Regression gate for source-side array producer materialization.  Foreign
# target coverage is compile-only; host GCC independently links/runs x86.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-${CC:-gcc}}
positive="$root/tests/compiler/c0/array_local_materialization.c"
negative="$root/tests/compiler/c0/array_local_materialization_negative.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

test -x "$minic"
"$host_cc" -std=gnu11 -Wall -Wextra -Werror -fsyntax-only "$positive"
"$host_cc" -E -P -std=gnu11 "$positive" -o "$work/positive.i"

targets='riscv32-linux riscv64-linux x86_64-linux aarch64-linux loongarch64-linux'
for target in $targets; do
    "$minic" --target="$target" -S "$work/positive.i" -o "$work/$target.s"
    test -s "$work/$target.s"
done

"$host_cc" -std=gnu11 -O0 -fno-pie -no-pie "$positive" -o "$work/gnu-native"
"$host_cc" -fno-pie -no-pie -x assembler "$work/x86_64-linux.s" \
    -o "$work/minic-native"

# Exit 42 is intentional: a silent return 0 is not a passing semantic probe.
for variant in gnu-native minic-native; do
    set +e
    "$work/$variant" >"$work/$variant.stdout" 2>"$work/$variant.stderr"
    rc=$?
    set -e
    if test "$rc" -ne 42; then
        printf '%s\n' "ARRAY_LOCAL_MATERIALIZATION=FAIL native=$variant exit=$rc expected=42" >&2
        exit 1
    fi
done
cmp "$work/gnu-native.stdout" "$work/minic-native.stdout"
cmp "$work/gnu-native.stderr" "$work/minic-native.stderr"

# If an immutable pre-refactor binary is provided, a representation-only
# change must preserve each emitted .s exactly for this fixed fixture.  This
# optional A/B is not needed for a normal checkout's check-fast target.
if test -n "${MINIC_BASELINE_CC:-}"; then
    test -x "$MINIC_BASELINE_CC"
    for target in $targets; do
        "$MINIC_BASELINE_CC" --target="$target" -S "$work/positive.i" \
            -o "$work/$target.baseline.s"
        cmp "$work/$target.baseline.s" "$work/$target.s"
    done
fi

# GNU is the language-level rejection oracle; MiniC must reject all sixteen
# cases on every target and roll back its .s output transaction.
for case_no in $(seq 1 16); do
    if "$host_cc" -std=gnu11 -Wall -Wextra -Werror -fsyntax-only \
        -DCASE="$case_no" "$negative" >"$work/gnu-$case_no.log" 2>&1; then
        printf '%s\n' "ARRAY_LOCAL_MATERIALIZATION=FAIL GNU_ACCEPTED_CASE=$case_no" >&2
        exit 1
    fi
    "$host_cc" -E -P -std=gnu11 -DCASE="$case_no" "$negative" \
        -o "$work/negative-$case_no.i"
    for target in $targets; do
        output="$work/negative-$case_no-$target.s"
        if "$minic" --target="$target" -S "$work/negative-$case_no.i" \
            -o "$output" >"$work/minic-$case_no-$target.log" 2>&1; then
            printf '%s\n' "ARRAY_LOCAL_MATERIALIZATION=FAIL MINIC_ACCEPTED_CASE=$case_no target=$target" >&2
            exit 1
        fi
        if test -e "$output"; then
            printf '%s\n' "ARRAY_LOCAL_MATERIALIZATION=FAIL PARTIAL_OUTPUT_CASE=$case_no target=$target" >&2
            exit 1
        fi
    done
done

printf '%s\n' 'ARRAY_LOCAL_MATERIALIZATION=PASS targets=5 gnu_native=42 minic_x86_native=42 fixed_multidim=1 typedef_outer=1 inferred=1 const=1 parameter_adjustment=1 static_local=1 VLA_bound_once=1 compound=1 zero_local=1 negatives=80 rollback=80'
