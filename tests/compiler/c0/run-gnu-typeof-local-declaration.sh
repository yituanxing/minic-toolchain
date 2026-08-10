#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-typeof-local-declaration
assembly="$work/gnu_typeof_local_declaration.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_typeof_local_declaration.c" \
    -o "$work/gnu_typeof_local_declaration.i"
"$minic" -S "$work/gnu_typeof_local_declaration.i" -o "$assembly"

test -s "$assembly"
grep -F 'copy_next:' "$assembly" >/dev/null

printf '%s\n' 'PASS compiler/c0/gnu_typeof_local_declaration dispatch=shared-type-lookahead statement-expression=1 pointer-type=preserved'
