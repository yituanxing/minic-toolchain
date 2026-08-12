#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-conditional-null-pointer

rm -rf "$work"
mkdir -p "$work"

"$minic" -S "$root/tests/compiler/c0/conditional_null_pointer_constant.c" \
    -o "$work/conditional_null_pointer_constant.s"
test -s "$work/conditional_null_pointer_constant.s"
grep -F 'member_after_conditional:' "$work/conditional_null_pointer_constant.s" >/dev/null
grep -F 'linux_statement_expression_shape:' "$work/conditional_null_pointer_constant.s" >/dev/null

expect_failure() {
    source=$1
    message=$2
    if "$minic" -S "$root/tests/compiler/c0/$source.c" -o "$work/$source.s" \
        >"$work/$source.stdout" 2>"$work/$source.stderr"; then
        printf '%s\n' "FAIL compiler/c0/$source: compilation unexpectedly succeeded" >&2
        exit 1
    fi
    grep -F "$message" "$work/$source.stderr" >/dev/null || {
        cat "$work/$source.stderr" >&2
        exit 1
    }
}

expect_failure invalid_conditional_typed_null_pointer 'conditional expression branches have incompatible types'
expect_failure invalid_conditional_nonnull_void_pointer 'pointer member access requires a pointer to record'

printf '%s\n' 'PASS compiler/c0/conditional_null_pointer_constant integer-zero=pointer-type void-cast-zero=pointer-type qualifiers=preserved statement-expression=1 typed-pointer-zero=not-npc nonnull-void=not-npc'
