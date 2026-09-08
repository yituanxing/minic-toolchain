#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-floating-prefix-update

rm -rf "$work"
mkdir -p "$work"

"$minic" -S "$root/tests/compiler/c0/floating_prefix_update.c" \
    -o "$work/floating_prefix_update.s"

grep -F 'decrement_timeout:' "$work/floating_prefix_update.s" >/dev/null
grep -F 'increment_ratio:' "$work/floating_prefix_update.s" >/dev/null
grep -F 'postfix_decrement_timeout:' "$work/floating_prefix_update.s" >/dev/null
grep -F 'postfix_increment_ratio:' "$work/floating_prefix_update.s" >/dev/null
grep -F '  fsub.d ' "$work/floating_prefix_update.s" >/dev/null
grep -F '  fadd.s ' "$work/floating_prefix_update.s" >/dev/null

printf '%s\n' 'PASS compiler/c0/floating_update prefix=2 postfix=2 float=1 double=1'
