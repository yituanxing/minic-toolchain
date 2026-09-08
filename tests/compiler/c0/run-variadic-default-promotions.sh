#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-variadic-default-promotions

rm -rf "$work"
mkdir -p "$work"

"$minic" -S "$root/tests/compiler/c0/variadic_default_promotions.c" \
    -o "$work/variadic_default_promotions.s"

grep -F 'pass_variadic_float:' "$work/variadic_default_promotions.s" >/dev/null
grep -F 'pass_variadic_char:' "$work/variadic_default_promotions.s" >/dev/null
grep -F '  fcvt.d.s ' "$work/variadic_default_promotions.s" >/dev/null
grep -F '  call variadic_sink' "$work/variadic_default_promotions.s" >/dev/null

printf '%s\n' 'PASS compiler/c0/variadic_default_promotions float-to-double=1 integer-promotion=1'
