#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-extern-fixed-integer-array

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/extern_fixed_integer_array.c" \
    -o "$work/extern_fixed_integer_array.i"
"$minic" -S "$work/extern_fixed_integer_array.i" \
    -o "$work/extern_fixed_integer_array.s"

grep -F '.globl table' "$work/extern_fixed_integer_array.s" >/dev/null
grep -F '  .byte 1' "$work/extern_fixed_integer_array.s" >/dev/null
grep -F '  .byte 2' "$work/extern_fixed_integer_array.s" >/dev/null
grep -F '  .byte 3' "$work/extern_fixed_integer_array.s" >/dev/null
test "$(grep -Fc '  .byte 0' "$work/extern_fixed_integer_array.s")" -eq 2
grep -F '.size table, 5' "$work/extern_fixed_integer_array.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/extern_fixed_integer_array extern-merge=1 bound=5 brace=3 zero-fill=2'
