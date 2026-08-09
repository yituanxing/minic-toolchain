#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-external-pointer-definitions

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/external_pointer_definition.c" \
    -o "$work/external_pointer_definition.i"
"$minic" -S \
    "$work/external_pointer_definition.i" \
    -o "$work/external_pointer_definition.s"

grep -F '.globl message' "$work/external_pointer_definition.s" >/dev/null
grep -F '.type message, @object' "$work/external_pointer_definition.s" >/dev/null
grep -F 'message:' "$work/external_pointer_definition.s" >/dev/null
grep -E '^  \.dword \.Lminic_string_[0-9]+$' "$work/external_pointer_definition.s" >/dev/null
grep -F '.size message, 8' "$work/external_pointer_definition.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/external_pointer_definition linkage=external initializer=string-relocation'
