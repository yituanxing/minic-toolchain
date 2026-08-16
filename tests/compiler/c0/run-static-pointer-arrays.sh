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

grep -F '.section .init.data' "$work/static_pointer_array.s" >/dev/null
grep -F 'levels:' "$work/static_pointer_array.s" >/dev/null
grep -F '  .dword start_a' "$work/static_pointer_array.s" >/dev/null
grep -F '  .dword start_b' "$work/static_pointer_array.s" >/dev/null
grep -F '.size levels, 16' "$work/static_pointer_array.s" >/dev/null
grep -F 'names:' "$work/static_pointer_array.s" >/dev/null
string_relocations=$(grep -c '^  \.dword \.Lminic_string_' "$work/static_pointer_array.s")
if test "$string_relocations" -ne 2; then
    echo "FAIL static_pointer_array string-relocations=$string_relocations expected=2" >&2
    cat "$work/static_pointer_array.s" >&2
    exit 1
fi
grep -F '  .dword 0' "$work/static_pointer_array.s" >/dev/null
grep -F '.size names, 24' "$work/static_pointer_array.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/static_pointer_array inferred-bound=3 object-relocations=2 null-tail=typed-pointer-zero suffix-section=1 extern-array-decay=2'
