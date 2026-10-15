#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/gnu-va-builtins
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c \
  "$root/tests/compiler/c0/gnu_builtin_va_start.c" -o "$work/good.i"
"$minic" -S "$work/good.i" -o "$work/good.s"
test -s "$work/good.s"
grep -E 'add(i)?[[:space:]]+a0,[[:space:]]*s0' "$work/good.s" >/dev/null

"$host_cc" -E -P -std=gnu11 -x c \
  "$root/tests/compiler/c0/gnu_builtin_va_start_wrong_last.c" -o "$work/bad.i"
set +e
"$minic" -S "$work/bad.i" -o "$work/bad.s" 2>"$work/bad.err"
status=$?
set -e
test "$status" -ne 0
grep -F 'second argument must be the last named parameter' "$work/bad.err" >/dev/null

printf '%s\n' 'PASS compiler/c0/gnu-va-builtins start=semantic end=semantic wrong-last=fail-closed'
