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
grep -F "  subw a0, t0, a0" \
    "$work/prefix_decrement_update.s" >/dev/null
printf '%s\n' "PASS compiler/c0/prefix_decrement_update"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/invalid_for_decrement_target.c" \
    -o "$work/invalid_for_decrement_target.i"
if "$minic" -S \
    "$work/invalid_for_decrement_target.i" \
    -o "$work/invalid_for_decrement_target.s" \
    >"$work/invalid_for_decrement_target.stdout" \
    2>"$work/invalid_for_decrement_target.stderr"; then
    printf '%s\n' \
        "FAIL compiler/c0/invalid_for_decrement_target: compilation unexpectedly succeeded" >&2
    exit 1
fi
grep -F "prefix decrement requires a modifiable integer local" \
    "$work/invalid_for_decrement_target.stderr" >/dev/null
printf '%s\n' "PASS compiler/c0/invalid_for_decrement_target"
