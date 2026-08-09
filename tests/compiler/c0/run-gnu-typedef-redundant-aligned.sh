#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-typedef-redundant-aligned
assembly="$work/gnu_typedef_redundant_aligned.s"
negative_i="$work/gnu_typedef_nonredundant_aligned.i"
negative_s="$work/gnu_typedef_nonredundant_aligned.s"
negative_err="$work/gnu_typedef_nonredundant_aligned.err"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_typedef_redundant_aligned.c" \
    -o "$work/gnu_typedef_redundant_aligned.i"
"$minic" -S "$work/gnu_typedef_redundant_aligned.i" -o "$assembly"
test -s "$assembly"
grep -F 'signed128_aligned_size:' "$assembly" >/dev/null
grep -F 'aligned_pair_size:' "$assembly" >/dev/null
grep -F '  li a0, 16' "$assembly" >/dev/null
grep -F '  li a0, 32' "$assembly" >/dev/null

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_typedef_nonredundant_aligned.c" \
    -o "$negative_i"
set +e
"$minic" -S "$negative_i" -o "$negative_s" 2>"$negative_err"
status=$?
set -e
test "$status" -ne 0
grep -F 'non-redundant GNU typedef alignment requires attributed-type support' \
    "$negative_err" >/dev/null

printf '%s\n' 'PASS compiler/c0/gnu_typedef_redundant_aligned natural-int128=16 accepted=1 abi-changing-int=16 rejected=1'
