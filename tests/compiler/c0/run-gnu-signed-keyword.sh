#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-signed-keyword
assembly="$work/gnu_signed_keyword.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_signed_keyword.c" \
    -o "$work/gnu_signed_keyword.i"
"$minic" -S "$work/gnu_signed_keyword.i" -o "$assembly"

test -s "$assembly"
grep -F 'widen_signed_aliases:' "$assembly" >/dev/null
# The signed-char argument must preserve sign when loaded from its one-byte local slot.
grep -F '  lb a0,' "$assembly" >/dev/null

printf '%s\n' 'PASS compiler/c0/gnu_signed_keyword aliases=__signed,__signed__ types=char,short,int,long'
