#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-record-alignment
assembly="$work/gnu_record_alignment.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_record_alignment.c" \
    -o "$work/gnu_record_alignment.i"
"$minic" -S "$work/gnu_record_alignment.i" -o "$assembly"

test -s "$assembly"
for symbol in pointer_aligned_size over_aligned_size over_aligned_holder_size over_aligned_holder_offset; do
    grep -F "$symbol:" "$assembly" >/dev/null
done
size8=$(grep -c '  li a0, 8' "$assembly" || true)
size16=$(grep -c '  li a0, 16' "$assembly" || true)
size32=$(grep -c '  li a0, 32' "$assembly" || true)
test "$size8" -ge 1
test "$size16" -ge 2
test "$size32" -ge 1

printf '%s\n' 'PASS compiler/c0/gnu_record_alignment sizeof-pointer=8 overalign=16 holder-offset=16 holder-size=32 shared-ice=1'
