#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-integer-constant-bitwise

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/integer_constant_bitwise.c" \
    -o "$work/integer_constant_bitwise.i"
"$minic" -S "$work/integer_constant_bitwise.i" \
    -o "$work/integer_constant_bitwise.s"

grep -F '.type bitwise_table, @object' "$work/integer_constant_bitwise.s" >/dev/null
grep -F '  .byte 10' "$work/integer_constant_bitwise.s" >/dev/null
grep -F '  .byte 7' "$work/integer_constant_bitwise.s" >/dev/null
grep -F '  .byte 255' "$work/integer_constant_bitwise.s" >/dev/null
grep -F '  .byte 2' "$work/integer_constant_bitwise.s" >/dev/null
grep -F '  .byte 8' "$work/integer_constant_bitwise.s" >/dev/null
test "$(grep -Fc '  .byte 0' "$work/integer_constant_bitwise.s")" -eq 3
sed -n '/^octal_permission:/,/^\.size/p' "$work/integer_constant_bitwise.s" | grep -F '  .word 420' >/dev/null
printf '%s\n' 'PASS compiler/c0/integer_constant_bitwise shifts=<<,>> bitwise=&,^,| unary=~,! bound=8 zero-fill=3 octal=0644->420'
