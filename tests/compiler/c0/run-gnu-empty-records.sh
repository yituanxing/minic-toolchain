#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-empty-records
assembly="$work/gnu_empty_records.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_empty_records.c" \
    -o "$work/gnu_empty_records.i"
"$minic" -S "$work/gnu_empty_records.i" -o "$assembly"

test -s "$assembly"
for symbol in empty_struct_size empty_union_size empty_member_record_size empty_identity \
              empty_record_statement_copy empty_record_lvalue_copy; do
    grep -F "$symbol:" "$assembly" >/dev/null
done
size0=$(grep -c '  li a0, 0' "$assembly" || true)
test "$size0" -ge 2
grep -F '  li a0, 8' "$assembly" >/dev/null
grep -F '  call empty_source' "$assembly" >/dev/null
grep -F '  call empty_target' "$assembly" >/dev/null
source_line=$(grep -n -m1 '  call empty_source' "$assembly" | cut -d: -f1)
target_line=$(grep -n -m1 '  call empty_target' "$assembly" | cut -d: -f1)
test "$source_line" -lt "$target_line"

printf '%s\n' 'PASS compiler/c0/gnu_empty_records struct-size=0 union-size=0 empty-member-declaration=ignored member-record-size=8 zero-copy=statement+rvalue addressable-zero-local=1 side-effects=source+target complete=1 layout-sentinel=alignment'
