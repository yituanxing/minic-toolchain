#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-zero-length-array
rm -rf "$work"; mkdir -p "$work"
"$host_cc" -E -P -x c "$root/tests/compiler/c0/gnu_zero_length_array.c" -o "$work/zero.i"
"$minic" -S "$work/zero.i" -o "$work/zero.s"
grep -F 'vm_numa_event' "$work/zero.s" >/dev/null || true
printf '%s\n' 'PASS compiler/c0/gnu_zero_length_array extern=1 incomplete-to-zero=1 sizeof=0 decay=1 type-identity=complete-zero'
"$host_cc" -E -P -x c "$root/tests/compiler/c0/invalid_zero_length_array_redeclaration.c" -o "$work/conflict.i"
if "$minic" -S "$work/conflict.i" -o "$work/conflict.s" >"$work/conflict.stdout" 2>"$work/conflict.stderr"; then
  echo 'FAIL zero-length vs positive-bound redeclaration unexpectedly compiled' >&2
  exit 1
fi
grep -F 'conflicting extern object redeclaration' "$work/conflict.stderr" >/dev/null
printf '%s\n' 'PASS compiler/c0/invalid_zero_length_array_redeclaration zero-vs-one=conflict'
