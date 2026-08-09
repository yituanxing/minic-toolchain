#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-local-inferred-array

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/static_local_inferred_array.c" \
    -o "$work/static_local_inferred_array.i"
"$minic" -S "$work/static_local_inferred_array.i" \
    -o "$work/static_local_inferred_array.s"

grep -Fx '.section .rodata' "$work/static_local_inferred_array.s" >/dev/null
grep -F '.type __minic_static_local_' "$work/static_local_inferred_array.s" >/dev/null
grep -F '  .byte 1' "$work/static_local_inferred_array.s" >/dev/null
grep -F '  .byte 6' "$work/static_local_inferred_array.s" >/dev/null
grep -E '\.size __minic_static_local_[^,]+, 7$' "$work/static_local_inferred_array.s" >/dev/null
if grep -F '.globl __minic_static_local_' "$work/static_local_inferred_array.s" >/dev/null; then
    echo 'inferred static local array leaked external linkage' >&2
    exit 1
fi
printf '%s\n' 'PASS compiler/c0/static_local_inferred_array element=uchar count=7 brace-constants=1 internal-rodata=1'
