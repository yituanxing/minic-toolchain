#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-pointer-arrays

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/static_pointer_array.c" \
    -o "$work/static_pointer_array.i"
"$minic" -S \
    "$work/static_pointer_array.i" \
    -o "$work/static_pointer_array.s"

grep -F 'names:' "$work/static_pointer_array.s" >/dev/null
grep -F '  .dword .Lminic_string_0' "$work/static_pointer_array.s" >/dev/null
grep -F '  .dword .Lminic_string_1' "$work/static_pointer_array.s" >/dev/null
grep -F '  .zero 8' "$work/static_pointer_array.s" >/dev/null
grep -F '.size names, 24' "$work/static_pointer_array.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/static_pointer_array inferred-bound=3 object-relocations=2 null-tail=1'
