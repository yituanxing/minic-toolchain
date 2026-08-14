#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-record-multi-declarators

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/record_multi_declarators.c" \
    -o "$work/record_multi_declarators.i"
"$minic" -S "$work/record_multi_declarators.i" \
    -o "$work/record_multi_declarators.s"

test -s "$work/record_multi_declarators.s"
grep -F 'read_link:' "$work/record_multi_declarators.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/record_multi_declarators scalar-pair=1 self-pointer-pair=1'
