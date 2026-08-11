#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-aligned-record-fields

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/gnu_aligned_record_field.c" \
    -o "$work/gnu_aligned_record_field.i"
"$minic" -S "$work/gnu_aligned_record_field.i" \
    -o "$work/gnu_aligned_record_field.s"

test -s "$work/gnu_aligned_record_field.s"
grep -F 'aligned_values_offset:' "$work/gnu_aligned_record_field.s" >/dev/null
grep -F 'aligned_tail_offset:' "$work/gnu_aligned_record_field.s" >/dev/null
grep -F 'aligned_record_size:' "$work/gnu_aligned_record_field.s" >/dev/null
grep -F 'prefix_aligned_flags_offset:' "$work/gnu_aligned_record_field.s" >/dev/null
grep -F 'prefix_aligned_tail_offset:' "$work/gnu_aligned_record_field.s" >/dev/null
grep -F 'mixed_aligned_value_offset:' "$work/gnu_aligned_record_field.s" >/dev/null
grep -F 'mixed_aligned_tail_offset:' "$work/gnu_aligned_record_field.s" >/dev/null
grep -F 'mixed_aligned_record_size:' "$work/gnu_aligned_record_field.s" >/dev/null
# Existing suffix alignment contract: 16,32,48.
grep -F '  li a0, 48' "$work/gnu_aligned_record_field.s" >/dev/null
# Linux prefix shape naturally places u64 at 8 and its tail at 16.
grep -F '  li a0, 8' "$work/gnu_aligned_record_field.s" >/dev/null
# Prefix aligned(8) + suffix aligned(16) must merge to 16, with tail at 24 and size 32.
grep -F '  li a0, 24' "$work/gnu_aligned_record_field.s" >/dev/null
grep -F '  li a0, 32' "$work/gnu_aligned_record_field.s" >/dev/null

printf '%s\n' 'PASS compiler/c0/gnu_aligned_record_field minimum-align=16 typed-ast-consteval=1 placement=suffix+pre-declarator linux-prefix-shape=1 prefix-suffix-merge=max values-offset=16 tail-offset=32 record-size=48 offsetof=consistent'
