#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-builtin-expect

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/builtin_expect.c" \
    -o "$work/builtin_expect.i"
"$minic" -S "$work/builtin_expect.i" -o "$work/builtin_expect.s"

test -s "$work/builtin_expect.s"
grep -F 'expect_value:' "$work/builtin_expect.s" >/dev/null
grep -F 'expect_zero:' "$work/builtin_expect.s" >/dev/null
if grep -F '__builtin_expect' "$work/builtin_expect.s" >/dev/null; then
    printf '%s\n' 'FAIL compiler/c0/builtin_expect emitted runtime builtin symbol' >&2
    exit 1
fi

printf '%s\n' 'PASS compiler/c0/builtin_expect value=first-argument return-type=long hint=integer-constant runtime-call=none'
