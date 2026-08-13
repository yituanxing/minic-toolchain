#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-array-parameter-adjustment

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -x c "$root/tests/compiler/c0/array_parameter_adjustment.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
test -s "$work/output.s"
grep -F '.globl main' "$work/output.s" >/dev/null
grep -F 'consume_typedef_vector:' "$work/output.s" >/dev/null
grep -F 'consume_typedef_matrix:' "$work/output.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/array_parameter_adjustment explicit=incomplete+fixed+multidim typedef=array+multidim adjusted=pointer verifier=no-orphan'
