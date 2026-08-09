#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-function-pointer-parameters

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/function_pointer_parameter.c" \
    -o "$work/function_pointer_parameter.i"
"$minic" -S \
    "$work/function_pointer_parameter.i" \
    -o "$work/function_pointer_parameter.s"
grep -F '.type callback, @function' "$work/function_pointer_parameter.s" >/dev/null
grep -F '  la a0, callback' "$work/function_pointer_parameter.s" >/dev/null
grep -F '  call accept_callback' "$work/function_pointer_parameter.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/function_pointer_parameter direct-declarator=void-callback'
