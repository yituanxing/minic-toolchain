#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
cc=${HOST_CC:-cc}
work=${BUILD_DIR:-"$root/build/debug"}/tests/braced-static-scalar
mkdir -p "$work"
"$cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/braced_static_scalar.c" -o "$work/positive.i"
"$minic" -S "$work/positive.i" -o "$work/positive.s"
grep -F 'value:' "$work/positive.s" >/dev/null
grep -F 'pointer:' "$work/positive.s" >/dev/null
"$cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/invalid_braced_static_scalar_extra.c" -o "$work/negative.i"
if "$minic" -S "$work/negative.i" -o "$work/negative.s" 2>"$work/negative.err"; then exit 1; fi
grep -F "expected '}' after static scalar initializer" "$work/negative.err" >/dev/null
printf '%s\n' 'PASS compiler/c0/braced-static-scalar scalar=integer,pointer trailing-comma=1 excess=reject'
