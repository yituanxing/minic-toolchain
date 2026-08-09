#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-long-long-types

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/long_long_types.c" -o "$work/long_long_types.i"
"$minic" -S "$work/long_long_types.i" -o "$work/long_long_types.s"
grep -F '  .dword 3' "$work/long_long_types.s" >/dev/null
grep -F '  .dword 4' "$work/long_long_types.s" >/dev/null
grep -F '.type add_long_long, @function' "$work/long_long_types.s" >/dev/null
grep -F '.type add_unsigned_long_long, @function' "$work/long_long_types.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/long_long_types width=8 suffixes=LL,ULL rank=distinct'

"$host_cc" -E -P -x c "$root/tests/compiler/c0/invalid_too_many_long_specifiers.c" \
    -o "$work/invalid_too_many_long_specifiers.i"
if "$minic" -S "$work/invalid_too_many_long_specifiers.i" \
    -o "$work/invalid_too_many_long_specifiers.s" \
    >"$work/invalid_too_many_long_specifiers.stdout" \
    2>"$work/invalid_too_many_long_specifiers.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/invalid_too_many_long_specifiers: compilation unexpectedly succeeded' >&2
    exit 1
fi
grep -F 'too many long type specifiers' "$work/invalid_too_many_long_specifiers.stderr" >/dev/null
printf '%s\n' 'PASS compiler/c0/invalid_too_many_long_specifiers'
