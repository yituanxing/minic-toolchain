#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-block-extern-multi
mkdir -p "$work"
"$host_cc" -E -P -x c "$root/tests/compiler/c0/block_scope_extern_multi_declarator.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
grep -F '__init_begin' "$work/output.s" >/dev/null
grep -F '__init_end' "$work/output.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/block_scope_extern_multi_declarator declarators=2 incomplete-arrays=2 scoped-bindings=2 entity-merge=transactional-array-descriptors'
