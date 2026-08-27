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

# Core represents prefix updates as integer ADD/SUB value operations and
# bitwise complement as INTEGER_BITWISE_NOT. Keep those semantic opcodes exact
# while leaving temporary-register allocation to the RV64 backend.
grep -E '^[[:space:]]+add[[:space:]]+' "$work/expression_statement_entry.s" >/dev/null
grep -E '^[[:space:]]+sub[[:space:]]+' "$work/expression_statement_entry.s" >/dev/null
grep -E '^[[:space:]]+xori[[:space:]]+[^,]+,[[:space:]]*[^,]+,[[:space:]]*-1$'     "$work/expression_statement_entry.s" >/dev/null

printf '%s\n' "PASS compiler/c0/expression_statement_entry owner=expression-parser prefix=++,-- unary=~ literal=char,float,string query=sizeof,alignof linux-member-prefix=1"
