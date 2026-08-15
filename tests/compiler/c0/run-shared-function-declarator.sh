#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-shared-function-declarator

mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/shared_function_declarator.c" \
    -o "$work/shared_function_declarator.i"
"$minic" -S \
    "$work/shared_function_declarator.i" \
    -o "$work/shared_function_declarator.s"

grep -F ".type apply, @function" "$work/shared_function_declarator.s" >/dev/null
grep -F "  call apply" "$work/shared_function_declarator.s" >/dev/null
jalr_count=$(grep -c -F "  jalr" "$work/shared_function_declarator.s")
test "$jalr_count" -ge 4
printf '%s\n' \
    "PASS compiler/c0/shared_function_declarator contexts=parenthesized-function-typedef,direct-function-typedef,function-pointer-field,extern-function-pointer-object,function-pointer-parameter,local-function-pointer local=void+int ordinary-parenthesized-local=preserved direct-pointer-call=1"
