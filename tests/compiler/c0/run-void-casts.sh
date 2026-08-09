#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-void-casts

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/void_cast_discard.c" -o "$work/void_cast_discard.i"
"$minic" -S "$work/void_cast_discard.i" -o "$work/void_cast_discard.s"
grep -F '  call bump' "$work/void_cast_discard.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/void_cast_discard literal=discard call=side-effect-preserved'
