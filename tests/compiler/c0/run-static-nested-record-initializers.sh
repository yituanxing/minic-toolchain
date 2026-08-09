#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-nested-record-initializer
asm="$work/static_nested_record_initializer.s"

require_fixed() {
    needle=$1
    label=$2
    if ! grep -F "$needle" "$asm" >/dev/null; then
        echo "FAIL static_nested_record_initializer missing=$label needle=$needle" >&2
        cat "$asm" >&2
        exit 1
    fi
}

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/static_nested_record_initializer.c" \
    -o "$work/static_nested_record_initializer.i"
"$minic" -S "$work/static_nested_record_initializer.i" -o "$asm"

if ! test -s "$asm"; then
    echo 'FAIL static_nested_record_initializer empty-assembly' >&2
    exit 1
fi
require_fixed '.section .rodata' 'rodata-section'
require_fixed '.type dummy, @object' 'object-type'
require_fixed '  .byte 16' 'outer-tag'
require_fixed '  .byte 3' 'key-tag'
require_fixed '  .word 7' 'next-field'
require_fixed '.size dummy, 24' 'rv64-object-size'
require_fixed 'read_dummy:' 'member-access-codegen'
if grep -F '.globl dummy' "$asm" >/dev/null; then
    echo 'FAIL static_nested_record_initializer leaked-external-linkage' >&2
    cat "$asm" >&2
    exit 1
fi
printf '%s\n' 'PASS compiler/c0/static_nested_record_initializer union=2 struct=1 null-pointer=2 bitwise=1 size=24 internal-rodata=1'
