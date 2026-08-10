#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-assert-declaration

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/static_assert_declaration.c" \
    -o "$work/static_assert_declaration.i"
"$minic" -S "$work/static_assert_declaration.i" -o "$work/static_assert_declaration.s"
test -s "$work/static_assert_declaration.s"
grep -F 'generic_probe:' "$work/static_assert_declaration.s" >/dev/null
grep -F 'main:' "$work/static_assert_declaration.s" >/dev/null

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/invalid_static_assert_false.c" \
    -o "$work/invalid_static_assert_false.i"
if "$minic" -S "$work/invalid_static_assert_false.i" \
    -o "$work/invalid_static_assert_false.s" 2>"$work/invalid_static_assert_false.stderr"; then
    printf '%s\n' 'false _Static_assert unexpectedly compiled' >&2
    exit 1
fi
grep -F 'static assertion failed' "$work/invalid_static_assert_false.stderr" >/dev/null

printf '%s\n' 'PASS compiler/c0/static_assert_declaration scope=file+block condition=shared-ast-consteval builtin=types-compatible+typeof message=concatenated false=reject runtime=none'
