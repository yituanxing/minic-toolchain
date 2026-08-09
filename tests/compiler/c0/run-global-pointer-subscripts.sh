#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-global-pointer-subscripts

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/global_pointer_subscript.c" \
    -o "$work/global_pointer_subscript.i"
"$minic" -S \
    "$work/global_pointer_subscript.i" \
    -o "$work/global_pointer_subscript.s"

grep -F '  la a0, items' "$work/global_pointer_subscript.s" >/dev/null
grep -F '  ld a0, 0(a0)' "$work/global_pointer_subscript.s" >/dev/null
grep -F '  slli a0, a0, 3' "$work/global_pointer_subscript.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/global_pointer_subscript base=global-pointer element=pointer stride=8'
