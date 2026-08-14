#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-pointer-compound-subtraction

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/pointer_compound_subtraction.c" \
    -o "$work/pointer_compound_subtraction.i"
"$minic" -S "$work/pointer_compound_subtraction.i" \
    -o "$work/pointer_compound_subtraction.s"

test -s "$work/pointer_compound_subtraction.s"
grep -F 'read_adjusted:' "$work/pointer_compound_subtraction.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/pointer_compound_subtraction plus-equal=1 minus-equal=1 complete-pointee=1'
