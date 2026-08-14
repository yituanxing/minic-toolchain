#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-wide-string

mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -fshort-wchar -funsigned-char -x c \
    "$root/tests/compiler/c0/wide_string_literal.c" \
    -o "$work/wide_string_literal.i"
"$minic" -S "$work/wide_string_literal.i" -o "$work/wide_string_literal.s"

grep -F 'wide_efi_call:' "$work/wide_string_literal.s" >/dev/null
grep -F 'Lvalue_boundary:' "$work/wide_string_literal.s" >/dev/null
grep -F '  .half 83' "$work/wide_string_literal.s" >/dev/null
grep -F '  .half 0' "$work/wide_string_literal.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/wide_string_literal encoding=L element=unsigned-short sizeof=target-aware indirect-call=typed'

expect_failure() {
    name=$1
    message=$2

    "$host_cc" -E -P -std=gnu11 -fshort-wchar -funsigned-char -x c \
        "$root/tests/compiler/c0/$name.c" \
        -o "$work/$name.i"
    if "$minic" -S "$work/$name.i" -o "$work/$name.s" \
        >"$work/$name.stdout" 2>"$work/$name.stderr"; then
        printf '%s\n' "FAIL compiler/c0/$name: compilation unexpectedly succeeded" >&2
        exit 1
    fi
    if ! grep -F "$message" "$work/$name.stderr" >/dev/null; then
        printf '%s\n' "FAIL compiler/c0/$name: diagnostic mismatch" >&2
        cat "$work/$name.stderr" >&2
        exit 1
    fi
    printf '%s\n' "PASS compiler/c0/$name"
}

expect_failure invalid_indirect_nested_diagnostic 'use of undeclared local'
expect_failure invalid_mixed_string_encoding 'mixed string literal encodings are not supported yet'
