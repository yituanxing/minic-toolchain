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
    "$root/tests/compiler/c0/parameter_suffix_informational_attribute.c" \
    -o "$work/parameter-suffix-info.i"
"$minic" -S "$work/parameter-suffix-info.i" -o "$work/parameter-suffix-info.s"
grep -F 'add_one:' "$work/parameter-suffix-info.s" >/dev/null

"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/invalid_parameter_suffix_aligned.c" \
    -o "$work/parameter-suffix-aligned.i"
if "$minic" -S "$work/parameter-suffix-aligned.i" -o "$work/parameter-suffix-aligned.s" \
    >"$work/parameter-suffix-aligned.out" 2>"$work/parameter-suffix-aligned.err"; then
    echo 'FAIL compiler/c0/invalid_parameter_suffix_aligned' >&2
    exit 1
fi
grep -F 'GNU parameter declarator attribute requires explicit language/layout semantics' \
    "$work/parameter-suffix-aligned.err" >/dev/null
printf '%s\n' 'PASS compiler/c0/parameter_suffix_informational_attribute informational=1 layout=fail-closed'

"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/direct_function_parameter.c" \
    -o "$work/direct-function.i"
"$minic" -S "$work/direct-function.i" -o "$work/direct-function.s"
grep -F 'apply:' "$work/direct-function.s" >/dev/null
grep -F 'jalr' "$work/direct-function.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/direct_function_parameter declarator=function-adjusted-to-pointer'
