#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/incomplete-array-typedef
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c \
  "$root/tests/compiler/c0/incomplete_array_typedef.c" -o "$work/good.i"
"$minic" -S "$work/good.i" -o "$work/good.s"
test -s "$work/good.s"

# The extension is intentionally only the outermost incomplete array owner.
# An array element itself may not be an incomplete array type.
set +e
"$host_cc" -E -P -std=gnu11 -x c \
  "$root/tests/compiler/c0/incomplete_array_typedef_nested_bad.c" -o "$work/bad.i" 2>/dev/null
host_status=$?
set -e
if test "$host_status" -eq 0; then
  set +e
  "$minic" -S "$work/bad.i" -o "$work/bad.s" 2>"$work/bad.err"
  status=$?
  set -e
  test "$status" -ne 0
  grep -F 'only the outermost array dimension may be incomplete' "$work/bad.err" >/dev/null
fi

printf '%s\n' 'PASS compiler/c0/incomplete-array-typedef owner=typedef nested-incomplete=fail-closed'
