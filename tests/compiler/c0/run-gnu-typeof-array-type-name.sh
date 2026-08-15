#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-typeof-array-type-name

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c \
  "$root/tests/compiler/c0/gnu_typeof_array_type_name.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
test -s "$work/output.s"
grep -F 'typeof_array_size:' "$work/output.s" >/dev/null
grep -F 'typeof_pointer_array_size:' "$work/output.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/gnu_typeof_array_type_name array=constant-bound pointer-array=1 sizeof=1'
