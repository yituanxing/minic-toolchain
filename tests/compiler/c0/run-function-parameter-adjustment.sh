#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-function-parameter-adjustment

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c   "$root/tests/compiler/c0/function_parameter_adjustment.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
test -s "$work/output.s"

grep -F 'apply_callback:' "$work/output.s" >/dev/null
grep -F 'invoke_done:' "$work/output.s" >/dev/null
grep -F 'jalr' "$work/output.s" >/dev/null

printf '%s
'   'PASS compiler/c0/function_parameter_adjustment typedef-function=pointer-adjusted declaration-pointer-redeclaration=compatible definition=1 indirect-call=1 sizeof=pointer'

"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/direct_function_parameter.c" \
    -o "$work/direct-function.i"
"$minic" -S "$work/direct-function.i" -o "$work/direct-function.s"
grep -F 'apply:' "$work/direct-function.s" >/dev/null
grep -F 'jalr' "$work/direct-function.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/direct_function_parameter declarator=function-adjusted-to-pointer'
