#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-for-expression-initializer

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/for_expression_initializer.c" \
    -o "$work/for_expression_initializer.i"
"$minic" -S "$work/for_expression_initializer.i" \
    -o "$work/for_expression_initializer.s"

grep -F 'for_compound_initializer:' "$work/for_expression_initializer.s" >/dev/null
grep -F 'for_comma_initializer:' "$work/for_expression_initializer.s" >/dev/null
grep -F 'for_parenthesized_post_update:' "$work/for_expression_initializer.s" >/dev/null
grep -F 'for_comma_update:' "$work/for_expression_initializer.s" >/dev/null
grep -E '^[[:space:]]+subw[[:space:]]+a0,' "$work/for_expression_initializer.s" >/dev/null
grep -F '  beqz a0, .Lwhile_end_' "$work/for_expression_initializer.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/for_expression_initializer compound=-= expression-init=general comma-init=2 update=full-expression,parenthesized-post++,comma-expression'
