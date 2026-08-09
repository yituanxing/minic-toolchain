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
grep -F '  lhu a0, 0(a0)' "$work/short_integer_layout.s" >/dev/null
grep -F '  sh t0, 0(t1)' "$work/short_integer_layout.s" >/dev/null
grep -F '  slli a0, a0, 48' "$work/short_integer_layout.s" >/dev/null
grep -F '  srai a0, a0, 48' "$work/short_integer_layout.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/short_integer size=2 align=2 unsigned-load=lhu store=sh signed-extension=16'
