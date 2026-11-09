#!/bin/sh
# All five targets must preserve the same source array contract.  The host
# provides the GNU C baseline and an exact x86-64 AS/LD/native runtime oracle;
# this local probe makes no claim about foreign-ISA QEMU execution.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-${CC:-gcc}}
positive="$root/tests/compiler/c0/array_shape_convergence.c"
negative="$root/tests/compiler/c0/array_shape_convergence_negative.c"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

test -x "$minic"
"$host_cc" -std=gnu11 -Wall -Wextra -Werror -fsyntax-only "$positive"
"$host_cc" -E -P -std=gnu11 "$positive" -o "$work/positive.i"

for target in riscv32-linux riscv64-linux x86_64-linux aarch64-linux loongarch64-linux; do
    "$minic" --target="$target" -S "$work/positive.i" -o "$work/$target.s"
    test -s "$work/$target.s"
done

"$host_cc" -std=gnu11 -O0 -fno-pie -no-pie "$positive" -o "$work/gnu-native"
"$host_cc" -fno-pie -no-pie -x assembler "$work/x86_64-linux.s" -o "$work/minic-native"
"$work/gnu-native"
"$work/minic-native"

# Five different source-level failures, accepted by neither GNU nor MiniC.
# Compilers must not leave a partial assembly output after rejection.
for case_no in 1 2 3 4 5; do
    if "$host_cc" -std=gnu11 -Wall -Wextra -Werror -fsyntax-only \
        -DCASE="$case_no" "$negative" >"$work/gnu-$case_no.out" 2>&1; then
        echo "FAIL GNU accepted invalid array CASE=$case_no" >&2
        exit 1
    fi
    "$host_cc" -E -P -std=gnu11 -DCASE="$case_no" "$negative" \
        -o "$work/negative-$case_no.i"
    for target in riscv32-linux riscv64-linux x86_64-linux aarch64-linux loongarch64-linux; do
        output="$work/invalid-$case_no-$target.s"
        if "$minic" --target="$target" -S "$work/negative-$case_no.i" \
            -o "$output" >"$work/minic-$case_no-$target.out" 2>&1; then
            echo "FAIL MiniC accepted invalid array CASE=$case_no target=$target" >&2
            exit 1
        fi
        if test -e "$output"; then
            echo "FAIL MiniC left partial output CASE=$case_no target=$target" >&2
            exit 1
        fi
    done
done

echo 'ARRAY_SHAPE_CONVERGENCE=PASS targets=5 gnu_native=1 minic_x86_native=1 nested_typedef=1 record=1 zero=1 flexible=1 vla_bound_once=1 compound=1 inferred=1 negative=25 no_partial_output=25'
