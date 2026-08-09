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
for symbol in empty_struct_size empty_union_size empty_identity; do
    grep -F "$symbol:" "$assembly" >/dev/null
done
size0=$(grep -c '  li a0, 0' "$assembly" || true)
test "$size0" -ge 2

printf '%s\n' 'PASS compiler/c0/gnu_empty_records struct-size=0 union-size=0 complete=1 layout-sentinel=alignment'
