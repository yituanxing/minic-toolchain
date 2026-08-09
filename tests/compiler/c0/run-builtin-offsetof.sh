#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-builtin-offsetof

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/builtin_offsetof.c" \
    -o "$work/builtin_offsetof.i"
"$minic" -S "$work/builtin_offsetof.i" -o "$work/builtin_offsetof.s"

grep -F '  li a0, 4' "$work/builtin_offsetof.s" >/dev/null
grep -F '  li a0, 8' "$work/builtin_offsetof.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/builtin_offsetof direct-member=1 typedef=1 target-layout=1'
