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
grep -F 'read_timer_like:' "$assembly" >/dev/null
if grep -F '.globl read_timer_like' "$assembly" >/dev/null; then
    printf '%s\n' 'FAIL compiler/c0/function_linkage_inheritance internal function was exported' >&2
    exit 1
fi
printf '%s\n' 'PASS compiler/c0/function_linkage_inheritance prior=static-inline later=non-static-declaration effective-linkage=internal export=none'
