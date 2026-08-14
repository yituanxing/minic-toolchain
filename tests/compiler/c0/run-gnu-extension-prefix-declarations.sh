#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-extension-prefix
assembly="$work/gnu_extension_prefix_declaration.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_extension_prefix_declaration.c" \
    -o "$work/gnu_extension_prefix_declaration.i"
"$minic" -S "$work/gnu_extension_prefix_declaration.i" -o "$assembly"

test -s "$assembly"
grep -F 'extension_sum:' "$assembly" >/dev/null

printf '%s\n' 'PASS compiler/c0/gnu_extension_prefix_declaration marker=diagnostic-only placement=before-typedef signed-alias=1'
