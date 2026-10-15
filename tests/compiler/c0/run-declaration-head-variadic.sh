#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/declaration-head-variadic
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c \
  "$root/tests/compiler/c0/declaration_head_variadic.c" -o "$work/good.i"
"$minic" -S "$work/good.i" -o "$work/good.s"
test -s "$work/good.s"
grep -F 'call pick_first' "$work/good.s" >/dev/null
grep -F '.globl main' "$work/good.s" >/dev/null

"$host_cc" -E -P -std=gnu11 -x c \
  "$root/tests/compiler/c0/declaration_head_variadic_layout_bad.c" -o "$work/bad.i"
set +e
"$minic" -S "$work/bad.i" -o "$work/bad.s" 2>"$work/bad.err"
status=$?
set -e
test "$status" -ne 0
grep -F 'unsupported GNU declaration-head typedef attribute' "$work/bad.err" >/dev/null

printf '%s\n' 'PASS compiler/c0/declaration-head-variadic attributes=semantic-target function-type=variadic call-tail=enabled layout=fail-closed'
