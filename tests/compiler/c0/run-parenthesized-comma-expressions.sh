#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-parenthesized-comma-expressions

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/parenthesized_comma_expression.c" \
    -o "$work/parenthesized_comma_expression.i"
"$minic" -S \
    "$work/parenthesized_comma_expression.i" \
    -o "$work/parenthesized_comma_expression.s"

test "$(grep -c -F '  call bump' "$work/parenthesized_comma_expression.s")" -eq 2
grep -F '  li a0, 11' "$work/parenthesized_comma_expression.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/parenthesized_comma_expression calls=2 result=rhs void-chain=1'
