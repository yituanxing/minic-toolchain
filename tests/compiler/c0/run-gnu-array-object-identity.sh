#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-array-object-identity
assembly="$work/gnu_array_object_identity.s"
rm -rf "$work"; mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_array_object_identity.c" -o "$work/gnu_array_object_identity.i"
"$minic" -S "$work/gnu_array_object_identity.i" -o "$assembly"
test -s "$assembly"
check_li() { symbol=$1; value=$2; sed -n "/^${symbol}:/,/^\\.size/p" "$assembly" | grep -F "  li a0, $value" >/dev/null; }
check_li fixed_member_size 32
check_li fixed_member_address_pointee_size 32
check_li fixed_member_typeof_size 32
check_li local_array_address_pointee_size 24
check_li local_array_typeof_size 24
sed -n '/linux_flexible_array_shape:/,/^\.size/p' "$assembly" | grep -F '  addi a0, a0, 8' >/dev/null
sed -n '/fixed_member_index:/,/^\.size/p' "$assembly" | grep -F '  slli a0, a0, 3' >/dev/null
for invalid in invalid_record_array_assignment invalid_record_array_update invalid_flexible_array_sizeof; do
  "$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/$invalid.c" -o "$work/$invalid.i"
  if "$minic" -S "$work/$invalid.i" -o "$work/$invalid.s" >"$work/$invalid.out" 2>"$work/$invalid.err"; then echo "expected $invalid to fail" >&2; exit 1; fi
done
grep -F 'assignment expression requires a modifiable object lvalue' "$work/invalid_record_array_assignment.err" >/dev/null
grep -F 'postfix update requires a modifiable scalar lvalue' "$work/invalid_record_array_update.err" >/dev/null
grep -F 'sizeof requires a supported complete type' "$work/invalid_flexible_array_sizeof.err" >/dev/null
printf '%s\n' 'PASS compiler/c0/gnu_array_object_identity record=fixed+flexible local=legacy decay=shared address-of=pointer-to-array sizeof=no-decay typeof=no-decay subscript=shared mutation=reject'
