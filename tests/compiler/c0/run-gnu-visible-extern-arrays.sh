#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-visible-extern-arrays

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/gnu_visible_extern_array.c" \
    -o "$work/gnu_visible_extern_array.i"
"$minic" -S "$work/gnu_visible_extern_array.i" \
    -o "$work/gnu_visible_extern_array.s"

test -s "$work/gnu_visible_extern_array.s"
grep -F '.globl names' "$work/gnu_visible_extern_array.s" >/dev/null
grep -F '.internal names' "$work/gnu_visible_extern_array.s" >/dev/null
grep -F '.size names, 24' "$work/gnu_visible_extern_array.s" >/dev/null

test "$(grep -c '^  .dword .Lminic_string_' "$work/gnu_visible_extern_array.s")" -ge 3
printf '%s\n' 'PASS compiler/c0/gnu_visible_extern_array declaration=fixed-pointer-array definition=merge visibility=internal size=24'
