#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-block-scope-extern-function-attributes
assembly="$work/block_scope_extern_function_attributes.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/block_scope_extern_function_attributes.c" \
    -o "$work/block_scope_extern_function_attributes.i"
"$minic" -S "$work/block_scope_extern_function_attributes.i" -o "$assembly"

test -s "$assembly"
grep -F '__compiletime_assert_0' "$assembly" >/dev/null

printf '%s\n' 'PASS compiler/c0/block_scope_extern_function_attributes scope=block linkage=extern prefix=noreturn suffix=error direct-call-resolved=1'
