#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-short-integers

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/short_integer_layout.c" \
    -o "$work/short_integer_layout.i"
"$minic" -S \
    "$work/short_integer_layout.i" \
    -o "$work/short_integer_layout.s"

grep -F '.size layout, 8' "$work/short_integer_layout.s" >/dev/null
grep -F '  .half 65535' "$work/short_integer_layout.s" >/dev/null
grep -Eq '^[[:space:]]+lhu[[:space:]]+[a-z][a-z0-9]*, 0\([a-z][a-z0-9]*\)$' "$work/short_integer_layout.s"
grep -Eq '^[[:space:]]+sh[[:space:]]+[a-z][a-z0-9]*, 0\([a-z][a-z0-9]*\)$' "$work/short_integer_layout.s"
grep -Eq '^[[:space:]]+slli[[:space:]]+[a-z][a-z0-9]*, [a-z][a-z0-9]*, 48$' "$work/short_integer_layout.s"
grep -Eq '^[[:space:]]+srai[[:space:]]+[a-z][a-z0-9]*, [a-z][a-z0-9]*, 48$' "$work/short_integer_layout.s"
printf '%s\n' 'PASS compiler/c0/short_integer size=2 align=2 unsigned-load=lhu store=sh signed-extension=16'
