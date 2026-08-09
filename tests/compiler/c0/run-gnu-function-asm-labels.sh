#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-function-asm-labels

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/gnu_function_asm_label.c" \
    -o "$work/gnu_function_asm_label.i"
"$minic" -S "$work/gnu_function_asm_label.i" \
    -o "$work/gnu_function_asm_label.s"

test -s "$work/gnu_function_asm_label.s"
grep -F '  call __real_renamed_api' "$work/gnu_function_asm_label.s" >/dev/null
grep -F '  la a0, __real_renamed_api' "$work/gnu_function_asm_label.s" >/dev/null
if grep -F '  call renamed_api' "$work/gnu_function_asm_label.s" >/dev/null; then
    printf '%s\n' 'FAIL compiler/c0/gnu_function_asm_labels emitted C identifier as linker symbol' >&2
    exit 1
fi

printf '%s\n' 'PASS compiler/c0/gnu_function_asm_labels declaration=1 adjacent-string=1 direct-call=linker-name function-address=linker-name'
