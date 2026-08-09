#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-zero-definitions

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/static_zero_definition.c" \
    -o "$work/static_zero_definition.i"
"$minic" -S \
    "$work/static_zero_definition.i" \
    -o "$work/static_zero_definition.s"

grep -F 'state:' "$work/static_zero_definition.s" >/dev/null
grep -F '.size state, 16' "$work/static_zero_definition.s" >/dev/null
grep -F 'counter:' "$work/static_zero_definition.s" >/dev/null
grep -F '.size counter, 4' "$work/static_zero_definition.s" >/dev/null
grep -F 'pointer:' "$work/static_zero_definition.s" >/dev/null
grep -F '.size pointer, 8' "$work/static_zero_definition.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/static_zero_definition record=16 int=4 pointer=8 implicit-zero=all'
