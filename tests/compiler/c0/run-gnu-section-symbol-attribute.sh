#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-section-symbol-attribute
assembly="$work/gnu_section_symbol_attribute.s"

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_section_symbol_attribute.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$assembly"
test -s "$assembly"
grep -F '.section .probe.data' "$assembly" >/dev/null
grep -F 'placed_data:' "$assembly" >/dev/null
grep -F '.section .probe.suffix.first' "$assembly" >/dev/null
grep -F 'suffix_first:' "$assembly" >/dev/null
grep -F '.section .probe.suffix.second' "$assembly" >/dev/null
grep -F 'suffix_second:' "$assembly" >/dev/null
grep -F '.section .probe.suffix.array' "$assembly" >/dev/null
grep -F 'suffix_array:' "$assembly" >/dev/null
grep -F '.section .probe.text' "$assembly" >/dev/null
grep -F 'placed_function:' "$assembly" >/dev/null
printf '%s\n' 'PASS compiler/c0/gnu_section_symbol_attribute extern-object=prefix+suffix per-declarator=isolated array-suffix=1 function-declaration=preserved definition-inherits=1 rv64-section-emission=1'
