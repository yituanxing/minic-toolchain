#!/bin/sh
set -eu
: "${MINIC_CC:?MINIC_CC path required}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
${CC:-cc} -E -P -x c "$root/tests/compiler/c0/float32_const_eval.c" \
    -o "$work/source.i"
"$MINIC_CC" --target=x86_64-linux -S "$work/source.i" -o "$work/source.s"
${CC:-cc} -no-pie "$work/source.s" -o "$work/minic"
"$work/minic"
${CC:-cc} -O0 "$root/tests/compiler/c0/float32_const_eval.c" -o "$work/gnu"
"$work/gnu"
echo 'FLOAT32_CONST_EVAL=PASS cast=1 arithmetic=3 unary=-2 gcc_oracle=1'
