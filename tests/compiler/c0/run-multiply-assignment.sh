#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-multiply-assignment

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/multiply_assignment.c" \
    -o "$work/multiply_assignment.i"
"$minic" -S \
    "$work/multiply_assignment.i" \
    -o "$work/multiply_assignment.s"

grep -F '  mul ' "$work/multiply_assignment.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/multiply_assignment operator=*= integer-rmw=yes'
