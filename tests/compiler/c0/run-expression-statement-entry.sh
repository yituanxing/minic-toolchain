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

# Core canonicalizes both integer prefix updates through INTEGER_ADD.
# Decrement forms a -1 delta with INTEGER_NEGATE, then uses the same add path.
# Bitwise complement remains INTEGER_BITWISE_NOT. Check those semantic opcodes
# without constraining temporary-register allocation.
test "$(grep -E -c '^[[:space:]]+add[[:space:]]+' "$work/expression_statement_entry.s")" -ge 2
grep -E '^[[:space:]]+neg[[:space:]]+' "$work/expression_statement_entry.s" >/dev/null
grep -E '^[[:space:]]+xori[[:space:]]+[^,]+,[[:space:]]*[^,]+,[[:space:]]*-1$'     "$work/expression_statement_entry.s" >/dev/null

printf '%s\n' "PASS compiler/c0/expression_statement_entry owner=expression-parser prefix=++,-- unary=~ literal=char,float,string query=sizeof,alignof linux-member-prefix=1"
