#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/static-union-zero-overlay
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/static_union_zero_overlay.c" -o "$work/valid.i"
"$minic" -S "$work/valid.i" -o "$work/valid.s"
test -s "$work/valid.s"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/static_union_nonzero_overlay_invalid.c" -o "$work/invalid.i"
if "$minic" -S "$work/invalid.i" -o "$work/invalid.s" >"$work/invalid.out" 2>"$work/invalid.err"; then
  echo "expected nonzero noncanonical union overlay rejection" >&2
  exit 1
fi
cat "$work/invalid.err"
grep -Fq 'backward noncanonical static union member requires a zero initializer' "$work/invalid.err"
echo 'PASS compiler/c0/static-union-zero-overlay zero-noncanonical=accepted nonzero=fail-closed'
