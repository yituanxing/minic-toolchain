#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-pointer-arrays

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/static_pointer_array.c" -o "$work/static_pointer_array.i"
"$minic" -S "$work/static_pointer_array.i" -o "$work/static_pointer_array.s"

grep -F '.section .init.data' "$work/static_pointer_array.s" >/dev/null
grep -F 'names:' "$work/static_pointer_array.s" >/dev/null
test "$(grep -Fc '  .dword .Lminic_string_' "$work/static_pointer_array.s")" -eq 2
grep -F '.size names, 24' "$work/static_pointer_array.s" >/dev/null
grep -F 'levels:' "$work/static_pointer_array.s" >/dev/null
grep -F '  .dword linker_start' "$work/static_pointer_array.s" >/dev/null
grep -F '.size levels, 8' "$work/static_pointer_array.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/static_pointer_array suffix-section=1 inferred=string+array-decay symbolic-reloc=3 null=1'
