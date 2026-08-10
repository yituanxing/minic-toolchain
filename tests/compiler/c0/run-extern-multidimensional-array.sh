#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-extern-multidimensional-array

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/extern_multidimensional_array.c" \
    -o "$work/extern_multidimensional_array.i"
"$minic" -S "$work/extern_multidimensional_array.i" \
    -o "$work/extern_multidimensional_array.s"

test -s "$work/extern_multidimensional_array.s"
grep -F 'read_cpu_bit:' "$work/extern_multidimensional_array.s" >/dev/null
grep -F 'cpu_bit_row:' "$work/extern_multidimensional_array.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/extern_multidimensional_array dimensions=2 bounds=constant-expression nested-type=Array(Array(ulong)) subscript=2 row-decay=pointer'
