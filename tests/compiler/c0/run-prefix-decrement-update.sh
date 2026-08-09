#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-prefix-decrement-update

mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/prefix_decrement_update.c" \
    -o "$work/prefix_decrement_update.i"
"$minic" -S \
    "$work/prefix_decrement_update.i" \
    -o "$work/prefix_decrement_update.s"
grep -F "  addi t0, t0, -1" \
    "$work/prefix_decrement_update.s" >/dev/null
printf '%s\n' "PASS compiler/c0/prefix_decrement_update"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/for_pointer_decrement_update.c" \
    -o "$work/for_pointer_decrement_update.i"
"$minic" -S \
    "$work/for_pointer_decrement_update.i" \
    -o "$work/for_pointer_decrement_update.s"
printf '%s\n' "PASS compiler/c0/for_pointer_decrement_update"
