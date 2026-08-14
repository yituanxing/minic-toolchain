#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-extension-declaration

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_extension_declaration.c" \
    -o "$work/gnu_extension_declaration.i"
"$minic" -S "$work/gnu_extension_declaration.i" \
    -o "$work/gnu_extension_declaration.s"

test -s "$work/gnu_extension_declaration.s"
grep -F 'read_wide:' "$work/gnu_extension_declaration.s" >/dev/null
grep -F 'wide_size:' "$work/gnu_extension_declaration.s" >/dev/null
grep -F '  li a0, 8' "$work/gnu_extension_declaration.s" >/dev/null

printf '%s\n' 'PASS compiler/c0/gnu_extension_declaration marker=diagnostic-only union-field=1 layout=unchanged size=8'
