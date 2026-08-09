#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-mixed-double-arithmetic

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/mixed_double_arithmetic.c" \
    -o "$work/mixed_double_arithmetic.i"
"$minic" -S "$work/mixed_double_arithmetic.i" \
    -o "$work/mixed_double_arithmetic.s"

grep -F '  fcvt.d.w ft0, t0' "$work/mixed_double_arithmetic.s" >/dev/null
grep -F '  fadd.d ft0, ft0, ft1' "$work/mixed_double_arithmetic.s" >/dev/null
grep -F '  fmul.d ft0, ft0, ft1' "$work/mixed_double_arithmetic.s" >/dev/null
grep -F '  fdiv.d ft0, ft0, ft1' "$work/mixed_double_arithmetic.s" >/dev/null
grep -F '  fsub.d ft0, ft0, ft1' "$work/mixed_double_arithmetic.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/mixed_double_arithmetic int-to-double=1 operators=+,-,*,/'
