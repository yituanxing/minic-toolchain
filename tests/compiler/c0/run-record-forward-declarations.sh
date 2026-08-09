#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-record-forward-declarations

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/record_forward_declarations.c" \
    -o "$work/record_forward_declarations.i"
"$minic" -S "$work/record_forward_declarations.i" \
    -o "$work/record_forward_declarations.s"

grep -F 'pass:' "$work/record_forward_declarations.s" >/dev/null
grep -F 'call pass' "$work/record_forward_declarations.s" >/dev/null
grep -F 'lw a0, 0(a0)' "$work/record_forward_declarations.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/record_forward_declarations struct=1 union=1 completion=same-tag pointer-before-complete=1'
