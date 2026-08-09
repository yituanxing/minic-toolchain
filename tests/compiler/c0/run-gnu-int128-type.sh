#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-int128-type
assembly="$work/gnu_int128_type.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_int128_type.c" \
    -o "$work/gnu_int128_type.i"
"$minic" -S "$work/gnu_int128_type.i" -o "$assembly"

test -s "$assembly"
for symbol in signed128_size unsigned128_size direct_unsigned128_size int128_record_size; do
    grep -F "$symbol:" "$assembly" >/dev/null
done
size16=$(grep -c '  li a0, 16' "$assembly" || true)
size32=$(grep -c '  li a0, 32' "$assembly" || true)
test "$size16" -ge 3
test "$size32" -ge 1

printf '%s\n' 'PASS compiler/c0/gnu_int128_type signed=1 unsigned=2 scalar-size=16 align=16 record-size=32'
