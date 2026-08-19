#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-character-array-string-init

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -x c "$root/tests/programs/c0/character_array_string_initializer.c" \
    -o "$work/character_array_string_initializer.i"
"$minic" -S "$work/character_array_string_initializer.i" \
    -o "$work/character_array_string_initializer.s"

grep -F '.type global_padded, @object' "$work/character_array_string_initializer.s" >/dev/null
grep -F '.size global_padded, 10' "$work/character_array_string_initializer.s" >/dev/null
grep -F '  .byte 114' "$work/character_array_string_initializer.s" >/dev/null
grep -F '  .byte 116' "$work/character_array_string_initializer.s" >/dev/null
grep -F '.size global_exact, 3' "$work/character_array_string_initializer.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/character-array-string-initializer static=fixed+exact runtime=fixed+inferred adjacent=1 escape=1 padding=1'
