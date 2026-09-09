#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-inline-keyword-spellings
assembly="$work/gnu_inline_keyword_spellings.s"

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_inline_keyword_spellings.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$assembly"
test -s "$assembly"
grep -F 'swap16:' "$assembly" >/dev/null
grep -F 'add1:' "$assembly" >/dev/null
printf '%s\n' 'PASS compiler/c0/gnu_inline_keyword_spellings spellings=__inline,__inline__'
