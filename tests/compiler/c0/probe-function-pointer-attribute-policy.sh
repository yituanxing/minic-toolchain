#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/release"}/tests/compiler-c0-function-pointer-attribute-policy
mkdir -p "$work"

invalid="$root/tests/compiler/c0/invalid_efi_msabi_function_pointer_attribute.c"

for stem in gnu_function_pointer_attribute_matrix gnu_function_pointer_msabi_parameter; do
    source="$root/tests/compiler/c0/$stem.c"
    "$host_cc" -std=gnu11 -Wall -Wextra -Werror "$source" -o "$work/$stem.gcc"
    "$work/$stem.gcc"
    "$host_cc" -std=gnu11 -E -P "$source" -o "$work/$stem.i"
    "$minic_cc" --target=x86_64-linux -S "$work/$stem.i" -o "$work/$stem.s"
    "$host_cc" -no-pie "$work/$stem.s" -o "$work/$stem.minic"
    "$work/$stem.minic"
done

for target in riscv32-linux riscv64-linux x86_64-linux aarch64-linux loongarch64-linux; do
    "$minic_cc" --target="$target" -S \
        "$work/gnu_function_pointer_attribute_matrix.i" \
        -o "$work/$target-portable.s"
    test -s "$work/$target-portable.s"
done

for target in riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
    if "$minic_cc" --target="$target" -S \
        "$work/gnu_function_pointer_msabi_parameter.i" \
        -o "$work/$target-msabi.s" >"$work/$target-msabi.out" \
        2>"$work/$target-msabi.err"; then
        echo "FAIL: target admitted x86 ms_abi type: $target" >&2
        exit 1
    fi
    grep -F 'unsupported GNU' "$work/$target-msabi.err" >/dev/null
done

for mode in BAD_TYPEDEF_ARGUMENT BAD_RECORD_ARGUMENT BAD_PARAMETER_ARGUMENT; do
    if "$host_cc" -std=gnu11 -Werror=attributes -fsyntax-only \
        "-D$mode" "$invalid" >"$work/$mode.gcc.out" \
        2>"$work/$mode.gcc.err"; then
        echo "FAIL: GCC admitted invalid ms_abi parameter: $mode" >&2
        exit 1
    fi
    "$host_cc" -std=gnu11 -E -P "-D$mode" "$invalid" -o "$work/$mode.i"
    if "$minic_cc" --target=x86_64-linux -S "$work/$mode.i" \
        -o "$work/$mode.s" >"$work/$mode.minic.out" \
        2>"$work/$mode.minic.err"; then
        echo "FAIL: MiniC admitted invalid ms_abi parameter: $mode" >&2
        exit 1
    fi
    grep -i 'GNU.*attribute' "$work/$mode.minic.err" >/dev/null
done

printf '%s\n' \
    'FUNCTION_POINTER_ATTRIBUTE_POLICY=PASS gcc_runtime=2 minic_x86_runtime=2 portable_targets=5 foreign_rejected=4 bad_attribute_rejected=3'
