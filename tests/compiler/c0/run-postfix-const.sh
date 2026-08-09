#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-postfix-const

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/postfix_const.c" -o "$work/postfix_const.i"
"$minic" -S "$work/postfix_const.i" -o "$work/postfix_const.s"
grep -F '.type same_first, @function' "$work/postfix_const.s" >/dev/null
grep -F '.type use_typedef_const, @function' "$work/postfix_const.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/postfix_const forms=char-const,const-char,typedef-const'
