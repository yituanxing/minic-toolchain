#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/record-pointer-array-field
mkdir -p "$work"
"$host_cc" -E -P -x c "$root/tests/compiler/c0/record_pointer_array_field.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
test -s "$work/output.s"
