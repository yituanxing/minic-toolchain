#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-prefix-function-attributes
assembly="$work/gnu_prefix_function_attributes.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_prefix_function_attributes.c" \
    -o "$work/gnu_prefix_function_attributes.i"
"$minic" -S "$work/gnu_prefix_function_attributes.i" -o "$assembly"

test -s "$assembly"
grep -F 'prefix_attribute_identity:' "$assembly" >/dev/null
grep -F 'call_prefix_attribute_identity:' "$assembly" >/dev/null

printf '%s\n' 'PASS compiler/c0/gnu_prefix_function_attributes prefix=1 metadata=unused,no-instrument gnu-inline=static-only'
