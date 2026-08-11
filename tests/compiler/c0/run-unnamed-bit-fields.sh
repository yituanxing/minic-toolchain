#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-unnamed-bit-fields
assembly="$work/unnamed_bit_fields.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/unnamed_bit_fields.c" \
    -o "$work/unnamed_bit_fields.i"
"$minic" -S "$work/unnamed_bit_fields.i" -o "$assembly"

test -s "$assembly"
grep -F 'full_unit_tail_offset:' "$assembly" >/dev/null
grep -F 'zero_width_tail_offset:' "$assembly" >/dev/null
grep -F '  li a0, 8' "$assembly" >/dev/null
grep -F '  li a0, 4' "$assembly" >/dev/null
grep -F '  li a0, 1' "$assembly" >/dev/null
grep -F '  li a0, 3' "$assembly" >/dev/null
grep -F '  li a0, 5' "$assembly" >/dev/null
sed -n '/read_bool_second:/,/^\.size/p' "$assembly" | grep -F 'lbu t6, 0(t5)' >/dev/null
sed -n '/read_bool_second:/,/^\.size/p' "$assembly" | grep -F 'srli a0, a0, 1' >/dev/null
sed -n '/write_bool_second:/,/^\.size/p' "$assembly" | grep -F 'sb t2, 0(t5)' >/dev/null
sed -n '/read_int_high:/,/^\.size/p' "$assembly" | grep -F 'lbu t6, 0(t5)' >/dev/null
sed -n '/read_int_high:/,/^\.size/p' "$assembly" | grep -F 'lbu t6, 1(t5)' >/dev/null
sed -n '/add_int_high:/,/^\.size/p' "$assembly" | grep -F 'sb t2, 0(t5)' >/dev/null
sed -n '/increment_barrier_second:/,/^\.size/p' "$assembly" | grep -F 'addi a0, a0, 4' >/dev/null

for invalid in invalid_bit_field_address invalid_named_zero_bit_field invalid_bit_field_width; do
    "$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/$invalid.c" -o "$work/$invalid.i"
    if "$minic" -S "$work/$invalid.i" -o "$work/$invalid.s" >"$work/$invalid.out" 2>"$work/$invalid.err"; then
        printf '%s\n' "expected $invalid to fail" >&2
        exit 1
    fi
done
grep -F 'cannot take the address of a bit-field' "$work/invalid_bit_field_address.err" >/dev/null
grep -F 'named bit-field width must be positive' "$work/invalid_named_zero_bit_field.err" >/dev/null
grep -F 'named bit-field width must be positive and fit its integer type' "$work/invalid_bit_field_width.err" >/dev/null

printf '%s\n' 'PASS compiler/c0/unnamed_bit_fields full-unit=1 zero-width=1 named-partial=bool+uint+ushort packing=little-endian boundary=type-alignment access=byte-rmw address-of=reject width=checked'
