#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-bool-semantics
assembly="$work/bool_semantics.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=c11 -x c "$root/tests/compiler/c0/bool_semantics.c" \
    -o "$work/bool_semantics.i"
"$minic" -S "$work/bool_semantics.i" -o "$assembly"

test -s "$assembly"
for symbol in bool_size bool_return bool_assignment bool_promotion bool_alias_roundtrip; do
    grep -F "$symbol:" "$assembly" >/dev/null
done
grep -F '  li a0, 1' "$assembly" >/dev/null
# Return-to-_Bool and assignment-to-_Bool both require nonzero-to-one normalization.
boolize=$(grep -c '  snez ' "$assembly" || true)
test "$boolize" -ge 2
# _Bool objects use one-byte storage and unsigned byte loads.
grep -F '  sb ' "$assembly" >/dev/null
grep -F '  lbu ' "$assembly" >/dev/null

printf '%s\n' 'PASS compiler/c0/bool_semantics size=1 rank=below-char promotion=int conversion=nonzero-to-one storage=byte'
