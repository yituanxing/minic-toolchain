#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-function-type-typedefs

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/function_type_typedef.c" \
    -o "$work/function_type_typedef.i"
"$minic" -S \
    "$work/function_type_typedef.i" \
    -o "$work/function_type_typedef.s"

grep -F '.type callback, @function' "$work/function_type_typedef.s" >/dev/null
grep -F '  la a0, callback' "$work/function_type_typedef.s" >/dev/null
grep -F '  call install_callback' "$work/function_type_typedef.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/function_type_typedef alias=function pointer-use=direct'
