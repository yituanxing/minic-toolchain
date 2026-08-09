#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-expression-statements

mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/programs/c0/expression_statement.c" \
    -o "$work/expression_statement.i"
"$minic" -S \
    "$work/expression_statement.i" \
    -o "$work/expression_statement.s"
grep -F "  call set_value" "$work/expression_statement.s" >/dev/null
grep -F "  call increment" "$work/expression_statement.s" >/dev/null
printf '%s\n' "PASS compiler/c0/expression_statement"

expect_failure() {
    name=$1
    diagnostic=$2

    "$host_cc" -E -P -x c \
        "$root/tests/compiler/c0/$name.c" \
        -o "$work/$name.i"
    if "$minic" -S "$work/$name.i" -o "$work/$name.s" \
        >"$work/$name.stdout" 2>"$work/$name.stderr"; then
        printf '%s\n' \
            "FAIL compiler/c0/$name: compilation unexpectedly succeeded" >&2
        exit 1
    fi
    grep -F "$diagnostic" "$work/$name.stderr" >/dev/null
    printf '%s\n' "PASS compiler/c0/$name"
}

expect_failure \
    invalid_call_assignment_target \
    "assignment expression requires a modifiable scalar lvalue"
expect_failure \
    invalid_expression_statement_semicolon \
    "expected ';' after expression"
