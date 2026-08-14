#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-function-visibility

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/gnu_function_visibility.c" \
    -o "$work/gnu_function_visibility.i"
"$minic" -S "$work/gnu_function_visibility.i" \
    -o "$work/gnu_function_visibility.s"

test -s "$work/gnu_function_visibility.s"
grep -F '.globl visible_api' "$work/gnu_function_visibility.s" >/dev/null
grep -F '.internal visible_api' "$work/gnu_function_visibility.s" >/dev/null
grep -F '  call visible_api' "$work/gnu_function_visibility.s" >/dev/null

printf '%s\n' 'PASS compiler/c0/gnu_function_visibility prefix=1 internal=ELF-directive redeclaration=consistent'
