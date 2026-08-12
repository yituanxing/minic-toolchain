#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug/expression-statement-entry"}
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/expression_statement_entry.c" \
    -o "$work/expression_statement_entry.i"
"$minic" -S "$work/expression_statement_entry.i" -o "$work/expression_statement_entry.s"

grep -F "  addi t0, t0, 1" "$work/expression_statement_entry.s" >/dev/null
grep -F "  addi t0, t0, -1" "$work/expression_statement_entry.s" >/dev/null
grep -F "  not a0, a0" "$work/expression_statement_entry.s" >/dev/null

printf '%s\n' "PASS compiler/c0/expression_statement_entry owner=expression-parser prefix=++,-- unary=~ literal=char,float,string query=sizeof,alignof linux-member-prefix=1"
