#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC_CC:-"$root/build/release/bin/minic-cc"}
host_cc=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

for name in gnu_local_wide_string_array gnu_record_array_chained_member; do
    source="$root/tests/compiler/c0/$name.c"
    "$host_cc" -std=gnu11 -fshort-wchar -O2 -no-pie "$source" -o "$work/$name.gnu"
    "$work/$name.gnu"
    "$host_cc" -E -P -std=gnu11 -fshort-wchar "$source" -o "$work/$name.i"
    for target in x86_64 aarch64 loongarch64 riscv64 riscv32; do
        "$minic" --target="$target-linux" -fshort-wchar -S "$work/$name.i" \
            -o "$work/$name.$target.s"
        test -s "$work/$name.$target.s"
    done
    "$host_cc" -no-pie -x assembler "$work/$name.x86_64.s" -o "$work/$name.minic"
    "$work/$name.minic"
    printf '%s\n' "PASS compiler/c0/$name GNU=1 five_isa=5 native_minic=1"

    bad="$root/tests/compiler/c0/${name}_negative.c"
    "$host_cc" -E -P -std=gnu11 -fshort-wchar "$bad" -o "$work/$name.bad.i"
    if "$host_cc" -std=gnu11 -fshort-wchar -c "$bad" -o "$work/$name.bad.o" \
        >"$work/gnu-bad.log" 2>&1; then
        printf '%s\n' "FAIL GNU accepted $name invalid initializer" >&2
        exit 1
    fi
    for target in x86_64 aarch64 loongarch64 riscv64 riscv32; do
        if "$minic" --target="$target-linux" -fshort-wchar -S "$work/$name.bad.i" \
            -o "$work/$name.bad.$target.s" >"$work/minic-bad.log" 2>&1; then
            printf '%s\n' "FAIL MiniC $target accepted $name invalid initializer" >&2
            exit 1
        fi
    done
    printf '%s\n' "PASS compiler/c0/$name negative_gnu=1 negative_5isa=5"
done
