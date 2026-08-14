#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-cast-type-classification

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/cast_type_classification.c" \
    -o "$work/cast_type_classification.i"
"$minic" -S "$work/cast_type_classification.i" \
    -o "$work/cast_type_classification.s"

test -s "$work/cast_type_classification.s"
grep -F 'read_union_cast:' "$work/cast_type_classification.s" >/dev/null
grep -F 'read_volatile_cast:' "$work/cast_type_classification.s" >/dev/null
grep -F 'narrow_short:' "$work/cast_type_classification.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/cast_type_classification short=1 union=1 volatile=1'
