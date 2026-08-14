#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-octal-character-case-range
assembly="$work/gnu_octal_character_case_range.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_octal_character_case_range.c" \
    -o "$work/gnu_octal_character_case_range.i"
"$minic" -S "$work/gnu_octal_character_case_range.i" -o "$assembly"

test -s "$assembly"
grep -F 'linux_printk_level_like:' "$assembly" >/dev/null
grep -F 'octal_character_values:' "$assembly" >/dev/null

printf '%s\n' 'PASS compiler/c0/gnu_octal_character_case_range octal=1-3-digits case-range=0...7 expanded=8 duplicate-check=preserved'
