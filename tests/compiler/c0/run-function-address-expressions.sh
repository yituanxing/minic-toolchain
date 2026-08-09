#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-function-address-expression

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/function_address_expression.c" \
    -o "$work/function_address_expression.i"
"$minic" -S "$work/function_address_expression.i" \
    -o "$work/function_address_expression.s"

grep -F '  la a0, increment' "$work/function_address_expression.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/function_address_expression direct=&fn cancel=&*fp RV64=function-address'
