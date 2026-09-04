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

expect_assembly_text() {
    text=$1
    if ! grep -F "$text" "$work/character_array_string_initializer.s" >/dev/null; then
        printf '%s\n' "FAIL compiler/c0/character-array-string-initializer missing=$text" >&2
        sed -n '1,220p' "$work/character_array_string_initializer.s" >&2
        exit 1
    fi
}

expect_assembly_text '.type global_padded, @object'
expect_assembly_text '.size global_padded, 10'
expect_assembly_text '  .byte 114'
expect_assembly_text '  .byte 116'
expect_assembly_text '.size global_exact, 3'
printf '%s\n' 'PASS compiler/c0/character-array-string-initializer static=fixed+exact runtime=fixed+inferred adjacent=1 escape=1 padding=1'
