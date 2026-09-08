#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-float-integer-arithmetic

rm -rf "$work"
mkdir -p "$work"

"$minic" -S "$root/tests/compiler/c0/float_integer_arithmetic.c" \
    -o "$work/float_integer_arithmetic.s"

grep -F 'scale_float:' "$work/float_integer_arithmetic.s" >/dev/null
grep -F 'add_float_int:' "$work/float_integer_arithmetic.s" >/dev/null
grep -F 'compare_float_int:' "$work/float_integer_arithmetic.s" >/dev/null
grep -F '  fdiv.s ' "$work/float_integer_arithmetic.s" >/dev/null
grep -F '  fadd.s ' "$work/float_integer_arithmetic.s" >/dev/null
grep -F '  fcvt.d.w ' "$work/float_integer_arithmetic.s" >/dev/null
grep -F '  fcvt.s.d ' "$work/float_integer_arithmetic.s" >/dev/null

printf '%s\n' 'PASS compiler/c0/float_integer_arithmetic usual-conversion=int-to-float arithmetic=+./ comparison=1'
