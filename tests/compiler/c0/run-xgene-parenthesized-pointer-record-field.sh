#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-xgene-parenthesized-pointer-record-field
mkdir -p "$work"

"$host_cc" -std=gnu11 -Werror -c \
    "$root/tests/compiler/c0/xgene_parenthesized_pointer_record_field.c" \
    -o "$work/gnu.o"
if "$host_cc" -std=gnu11 -Werror -c \
    "$root/tests/compiler/c0/invalid_xgene_parenthesized_pointer_record_field.c" \
    -o "$work/invalid-gnu.o" >"$work/invalid-gnu.out" 2>"$work/invalid-gnu.err"; then
    printf '%s\n' 'FAIL xgene invalid GNU declarator accepted' >&2
    exit 1
fi
"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/xgene_parenthesized_pointer_record_field.c" -o "$work/valid.i"
"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/invalid_xgene_parenthesized_pointer_record_field.c" \
    -o "$work/invalid.i"
for target in riscv32 riscv64 x86_64 aarch64 loongarch64; do
    "$minic_cc" "--target=$target-linux" -S "$work/valid.i" \
        -o "$work/valid.$target.s"
    if "$minic_cc" "--target=$target-linux" -S "$work/invalid.i" \
        -o "$work/invalid.$target.s" >"$work/invalid.$target.out" \
        2>"$work/invalid.$target.err"; then
        printf 'FAIL xgene invalid pointer field accepted target=%s\n' "$target" >&2
        exit 1
    fi
done

printf '%s\n' 'PASS compiler/c0/xgene-parenthesized-pointer-record-field GNU-valid+invalid differential 5ISA object-pointer+function-pointer+pointer-array'
