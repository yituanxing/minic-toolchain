#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-preformed-array-zero

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/static_preformed_array_zero.c" \
    -o "$work/static_preformed_array_zero.i"
"$minic" -S "$work/static_preformed_array_zero.i" \
    -o "$work/static_preformed_array_zero.s"

grep -F '.type plain_mask, @object' "$work/static_preformed_array_zero.s" >/dev/null
grep -F '.size plain_mask, 16' "$work/static_preformed_array_zero.s" >/dev/null
grep -F '.type percpu_mask, @object' "$work/static_preformed_array_zero.s" >/dev/null
grep -Fx '.section .data..percpu' "$work/static_preformed_array_zero.s" >/dev/null
grep -F '.size percpu_mask, 16' "$work/static_preformed_array_zero.s" >/dev/null
grep -F '.type integer_mask, @object' "$work/static_preformed_array_zero.s" >/dev/null
grep -F '.size integer_mask, 16' "$work/static_preformed_array_zero.s" >/dev/null
grep -F '.section .rodata' "$work/static_preformed_array_zero.s" >/dev/null
grep -F '.type readonly_mask, @object' "$work/static_preformed_array_zero.s" >/dev/null
grep -F '.size readonly_mask, 8' "$work/static_preformed_array_zero.s" >/dev/null
if grep -E '^\.globl (plain_mask|percpu_mask|integer_mask|readonly_mask)$' \
    "$work/static_preformed_array_zero.s" >/dev/null; then
    echo 'pre-formed static array leaked external linkage' >&2
    exit 1
fi
grep -F 'read_preformed_arrays:' "$work/static_preformed_array_zero.s" >/dev/null

printf '%s\n' 'PASS compiler/c0/static_preformed_array_zero typedef-array=1 typeof-array=1 section=1 zero-init=1 readonly=1'
