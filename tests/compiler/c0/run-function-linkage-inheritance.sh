#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-function-linkage-inheritance
assembly="$work/function_linkage_inheritance.s"

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/function_linkage_inheritance.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$assembly"
test -s "$assembly"
grep -F '.local read_timer_like' "$assembly" >/dev/null
grep -F 'read_timer_like:' "$assembly" >/dev/null
printf '%s\n' 'PASS compiler/c0/function_linkage_inheritance prior=static-inline later=non-static-declaration effective-linkage=internal'
