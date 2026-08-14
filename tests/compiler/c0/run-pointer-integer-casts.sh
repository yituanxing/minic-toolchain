#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-pointer-integer-casts

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/pointer_integer_casts.c" \
    -o "$work/pointer_integer_casts.i"
"$minic" -S "$work/pointer_integer_casts.i" \
    -o "$work/pointer_integer_casts.s"

grep -F '  la a0, value' "$work/pointer_integer_casts.s" >/dev/null
grep -F '  la a0, target' "$work/pointer_integer_casts.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/pointer_integer_casts object=1 function=1 roundtrip-through-ulong=1 implicit-assignment-unchanged=1'
