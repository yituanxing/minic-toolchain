#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-wide-integer-literals

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/wide_integer_literal.c" \
    -o "$work/wide_integer_literal.i"
"$minic" -S "$work/wide_integer_literal.i" -o "$work/wide_integer_literal.s"
grep -F '  li a0, 9223372036854775807' "$work/wide_integer_literal.s" >/dev/null
grep -F '  li a0, 9223372036854775806' "$work/wide_integer_literal.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/wide_integer_literal payload=int64 max=9223372036854775807'
