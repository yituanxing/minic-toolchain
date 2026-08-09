#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-inferred-char-array

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/static_inferred_char_array.c" \
    -o "$work/static_inferred_char_array.i"
"$minic" -S "$work/static_inferred_char_array.i" \
    -o "$work/static_inferred_char_array.s"

grep -F 'local_name:' "$work/static_inferred_char_array.s" >/dev/null
grep -F 'upvalue_name:' "$work/static_inferred_char_array.s" >/dev/null
grep -F '.size local_name, 6' "$work/static_inferred_char_array.s" >/dev/null
grep -F '.size upvalue_name, 8' "$work/static_inferred_char_array.s" >/dev/null
if grep -F '.globl local_name' "$work/static_inferred_char_array.s" >/dev/null; then
    echo 'static inferred character array leaked external linkage' >&2
    exit 1
fi
printf '%s\n' 'PASS compiler/c0/static_inferred_char_array concat=1 bounds=6,8 internal-linkage=1 terminator=included'
