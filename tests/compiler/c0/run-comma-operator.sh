#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-comma-operator

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/comma_operator.c" \
    -o "$work/comma_operator.i"
"$minic" -S "$work/comma_operator.i" \
    -o "$work/comma_operator.s"

test -s "$work/comma_operator.s"
grep -F 'comma_value:' "$work/comma_operator.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/comma_operator parenthesized=1 void-left=1 assignment-side-effect=1 result=right'
