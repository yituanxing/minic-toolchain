#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-statement-expression
assembly="$work/gnu_statement_expression.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_statement_expression.c" \
    -o "$work/gnu_statement_expression.i"
"$minic" -S "$work/gnu_statement_expression.i" -o "$assembly"

test -s "$assembly"
grep -F 'statement_value:' "$assembly" >/dev/null
grep -F '.Lwhile_condition_' "$assembly" >/dev/null

printf '%s\n' 'PASS compiler/c0/gnu_statement_expression scope=owned-block final-expression=value sequencing=expression-site loop=1'
