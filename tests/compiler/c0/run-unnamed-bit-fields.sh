#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-unnamed-bit-fields
assembly="$work/unnamed_bit_fields.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/unnamed_bit_fields.c" \
    -o "$work/unnamed_bit_fields.i"
"$minic" -S "$work/unnamed_bit_fields.i" -o "$assembly"

test -s "$assembly"
grep -F 'full_unit_tail_offset:' "$assembly" >/dev/null
grep -F 'zero_width_tail_offset:' "$assembly" >/dev/null
grep -F '  li a0, 8' "$assembly" >/dev/null
grep -F '  li a0, 4' "$assembly" >/dev/null

printf '%s\n' 'PASS compiler/c0/unnamed_bit_fields full-unit=int:32 tail-offset=8 zero-width=int:0 tail-offset=4 metadata=explicit partial=nonsupported'
