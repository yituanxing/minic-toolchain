#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-adjacent-string-literals

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/adjacent_string_literals.c" \
    -o "$work/adjacent_string_literals.i"
"$minic" -S \
    "$work/adjacent_string_literals.i" \
    -o "$work/adjacent_string_literals.s"

grep -F '  call consume' "$work/adjacent_string_literals.s" >/dev/null
test "$(grep -E -c '^\.Lminic_string_[0-9]+:$' "$work/adjacent_string_literals.s")" -eq 1
grep -E '^\.size \.Lminic_string_[0-9]+, 10$' "$work/adjacent_string_literals.s" >/dev/null
grep -F '  .byte 97' "$work/adjacent_string_literals.s" >/dev/null
grep -F '  .byte 98' "$work/adjacent_string_literals.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/adjacent_string_literals tokens=2 objects=1 bytes=9 nul=1 call-args=1'
