#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-inferred-integer-array

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/static_inferred_integer_array.c" \
    -o "$work/static_inferred_integer_array.i"
"$minic" -S "$work/static_inferred_integer_array.i" \
    -o "$work/static_inferred_integer_array.s"

grep -F 'runnable_avg_yN_inv:' "$work/static_inferred_integer_array.s" >/dev/null
grep -F '  .word 4294967295' "$work/static_inferred_integer_array.s" >/dev/null
grep -F '.size runnable_avg_yN_inv, 12' "$work/static_inferred_integer_array.s" >/dev/null
if grep -F '.globl runnable_avg_yN_inv' "$work/static_inferred_integer_array.s" >/dev/null; then
    echo 'inferred static integer array leaked external linkage' >&2
    exit 1
fi
printf '%s\n' 'PASS compiler/c0/static_inferred_integer_array bound=inferred-from-brace count=3 typed-bits=u32-max suffix-attribute=unused internal-linkage=1'
