#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/static-record-array-suffix-attributes
mkdir -p "$work"
"$host_cc" -E -P -x c "$root/tests/compiler/c0/static_record_array_suffix_attributes.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
grep -F '.section .data.test' "$work/output.s" >/dev/null
test -s "$work/output.s"
