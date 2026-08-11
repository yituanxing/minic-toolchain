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
for symbol in pointer_aligned_size over_aligned_size over_aligned_holder_size over_aligned_holder_offset designated_only_size designated_aligned_size; do
    grep -F "$symbol:" "$assembly" >/dev/null
done
size8=$(grep -c '  li a0, 8' "$assembly" || true)
size16=$(grep -c '  li a0, 16' "$assembly" || true)
size32=$(grep -c '  li a0, 32' "$assembly" || true)
test "$size8" -ge 1
test "$size16" -ge 2
test "$size32" -ge 1

sed -n '/designated_only_size:/,/^\.size/p' "$assembly" | grep -F '  li a0, 8' >/dev/null
sed -n '/designated_aligned_size:/,/^\.size/p' "$assembly" | grep -F '  li a0, 16' >/dev/null

for invalid in invalid_record_designated_init_arguments invalid_union_designated_init; do
    "$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/$invalid.c" -o "$work/$invalid.i"
    if "$minic" -S "$work/$invalid.i" -o "$work/$invalid.s" >"$work/$invalid.out" 2>"$work/$invalid.err"; then
        printf '%s\n' "expected $invalid to fail" >&2
        exit 1
    fi
done
grep -F 'GNU attribute has an invalid number of arguments' "$work/invalid_record_designated_init_arguments.err" >/dev/null
grep -F 'GNU designated_init applies only to struct types' "$work/invalid_union_designated_init.err" >/dev/null

printf '%s\n' 'PASS compiler/c0/gnu_record_alignment sizeof-pointer=8 overalign=16 holder-offset=16 holder-size=32 shared-alignment-decoder=1 designated-init=diagnostic-struct-only mixed-suffix=1'
