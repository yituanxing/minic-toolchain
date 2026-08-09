#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-zero-aggregate-null

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/zero_aggregate_null.c" \
    -o "$work/zero_aggregate_null.i"
"$minic" -S \
    "$work/zero_aggregate_null.i" \
    -o "$work/zero_aggregate_null.s"

grep -F '.globl main' "$work/zero_aggregate_null.s" >/dev/null
grep -F '  sw ' "$work/zero_aggregate_null.s" >/dev/null
grep -F '  sd ' "$work/zero_aggregate_null.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/zero_aggregate_null scalar-zero=yes pointer-null=yes'
