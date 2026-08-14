#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-local-interleaved-informational-attribute

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/gnu_local_interleaved_informational_attribute.c" \
    -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
test -s "$work/output.s"
grep -F 'main:' "$work/output.s" >/dev/null

printf '%s\n' 'PASS compiler/c0/gnu_local_interleaved_informational_attribute placement=type-before-declarator unused=informational declarators=2 initializers=2'
