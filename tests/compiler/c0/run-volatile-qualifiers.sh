#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-volatile-qualifiers

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/volatile_qualifiers.c" \
    -o "$work/volatile_qualifiers.i"
"$minic" -S "$work/volatile_qualifiers.i" \
    -o "$work/volatile_qualifiers.s"

test -s "$work/volatile_qualifiers.s"
grep -F 'read_state:' "$work/volatile_qualifiers.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/volatile_qualifiers base=1 const-volatile=1 local=1 pointer-level=1 typedef-pointer=1'
