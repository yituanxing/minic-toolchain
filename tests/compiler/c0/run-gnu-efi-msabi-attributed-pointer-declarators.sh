#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/release"}/tests/compiler-c0-gnu-efi-msabi-pointers

mkdir -p "$work"

# These match Linux 6.6.143 EFI's two independent GNU syntax forms:
# an ms_abi attribute inside an anonymous-record function pointer field,
# and the same attribute in a function pointer typedef declarator.
for stem in gnu_efi_record_msabi_prefix_callback gnu_efi_msabi_typedef_pointer \
            gnu_efi_prefix_msabi_prototype; do
    "$host_cc" -std=gnu11 -Wall -Wextra -Werror -fsyntax-only \
        "$root/tests/compiler/c0/$stem.c"
    "$host_cc" -std=gnu11 -E -P \
        "$root/tests/compiler/c0/$stem.c" -o "$work/$stem.i"
    MINIC_CORE_IR=strict "$minic_cc" --target=x86_64-linux -S \
        "$work/$stem.i" -o "$work/$stem.s"
    test -s "$work/$stem.s"
done

# Calling conventions are real ABI metadata. Do not accept ms_abi silently
# on other targets, or with an illegal GNU attribute argument list.
for target in riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
    for stem in gnu_efi_record_msabi_prefix_callback gnu_efi_msabi_typedef_pointer; do
        if "$minic_cc" --target="$target" -S "$work/$stem.i" \
            -o "$work/$target-$stem.s" >"$work/$target-$stem.out" \
            2>"$work/$target-$stem.err"; then
            echo "FAIL foreign target accepted EFI ms_abi field/typedef: $target/$stem" >&2
            exit 1
        fi
        grep -F 'unsupported GNU attribute' "$work/$target-$stem.err" >/dev/null
    done
done

for mode in BAD_TYPEDEF_ARGUMENT BAD_RECORD_ARGUMENT; do
    source="$root/tests/compiler/c0/invalid_efi_msabi_function_pointer_attribute.c"
    if "$host_cc" -std=gnu11 -Werror=attributes -fsyntax-only \
        "-D$mode" "$source" >"$work/$mode.gnu.out" 2>"$work/$mode.gnu.err"; then
        echo "FAIL GCC accepted invalid EFI ABI attribute: $mode" >&2
        exit 1
    fi
    "$host_cc" -std=gnu11 -E -P "-D$mode" "$source" -o "$work/$mode.i"
    if "$minic_cc" --target=x86_64-linux -S "$work/$mode.i" \
        -o "$work/$mode.s" >"$work/$mode.out" 2>"$work/$mode.err"; then
        echo "FAIL MiniC accepted invalid EFI ABI attribute: $mode" >&2
        exit 1
    fi
    grep -F 'unsupported GNU attribute in function-pointer' "$work/$mode.err" >/dev/null
done

printf '%s\n' 'PASS compiler/c0/gnu-efi-msabi-attributed-pointer-declarators GNU-positives=3 x86-strict-core=3 foreign-target-rejected=8 bad-attribute-rejected=2 gcc-oracle=1'
