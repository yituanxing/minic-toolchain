#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-local-string-arrays

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/static_local_string_array.c" \
    -o "$work/static_local_string_array.i"
"$minic" -S \
    "$work/static_local_string_array.i" \
    -o "$work/static_local_string_array.s"

grep -F '.Lminic_string_0:' "$work/static_local_string_array.s" >/dev/null
grep -F '.size .Lminic_string_0, 4' "$work/static_local_string_array.s" >/dev/null
grep -F '  .byte 97' "$work/static_local_string_array.s" >/dev/null
grep -F '  lbu a0, 0(a0)' "$work/static_local_string_array.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/static_local_string_array inferred-bound=4 initializer=string static-storage=yes'
