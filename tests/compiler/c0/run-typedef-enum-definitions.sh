#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-typedef-enum-definitions

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/typedef_enum_definition.c" \
    -o "$work/typedef_enum_definition.i"
"$minic" -S "$work/typedef_enum_definition.i" -o "$work/typedef_enum_definition.s"

grep -F '  li a0, 4' "$work/typedef_enum_definition.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/typedef_enum_definition anonymous=1 explicit-value=3 auto-next=4 alias=int'
