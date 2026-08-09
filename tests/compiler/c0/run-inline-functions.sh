#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-inline-functions

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/inline_function.c" -o "$work/inline_function.i"
"$minic" -S "$work/inline_function.i" -o "$work/inline_function.s"
grep -F '.type add_one, @function' "$work/inline_function.s" >/dev/null
grep -F '.type add_two, @function' "$work/inline_function.s" >/dev/null
grep -F '  call add_one' "$work/inline_function.s" >/dev/null
grep -F '  call add_two' "$work/inline_function.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/inline_function orders=static-inline,inline-static optimization=optional'

"$host_cc" -E -P -x c "$root/tests/compiler/c0/invalid_inline_object.c" -o "$work/invalid_inline_object.i"
if "$minic" -S "$work/invalid_inline_object.i" -o "$work/invalid_inline_object.s" \
    >"$work/invalid_inline_object.stdout" 2>"$work/invalid_inline_object.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/invalid_inline_object: compilation unexpectedly succeeded' >&2
    exit 1
fi
grep -F 'inline specifier requires a function declarator' "$work/invalid_inline_object.stderr" >/dev/null
printf '%s\n' 'PASS compiler/c0/invalid_inline_object'
