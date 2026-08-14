#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-object-address

rm -rf "$work"
mkdir -p "$work"

"$minic" -S "$root/tests/compiler/c0/static_object_address_relocation.c" \
    -o "$work/static_object_address_relocation.s"
test -s "$work/static_object_address_relocation.s"
grep -F 'external_address:' "$work/static_object_address_relocation.s" >/dev/null
grep -F '.dword external_address_target' "$work/static_object_address_relocation.s" >/dev/null
grep -F 'internal_address:' "$work/static_object_address_relocation.s" >/dev/null
count=$(grep -F -c '.dword internal_address_target' "$work/static_object_address_relocation.s")
test "$count" -eq 2
array_count=$(grep -F -c '.dword global_address_array' "$work/static_object_address_relocation.s")
test "$array_count" -eq 2
grep -F 'string_literal_address:' "$work/static_object_address_relocation.s" >/dev/null
grep -F '.dword .Lminic_string_' "$work/static_object_address_relocation.s" >/dev/null

if "$minic" -S "$root/tests/compiler/c0/invalid_static_object_address_type.c" \
    -o "$work/invalid-type.s" >"$work/invalid-type.stdout" 2>"$work/invalid-type.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/static-object-address: incompatible pointer type accepted' >&2
    exit 1
fi
grep -F 'static pointer initializer type mismatch' "$work/invalid-type.stderr" >/dev/null

if "$minic" -S "$root/tests/compiler/c0/invalid_static_object_address_addend.c" \
    -o "$work/invalid-addend.s" >"$work/invalid-addend.stdout" 2>"$work/invalid-addend.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/static-object-address: relocation addend accepted without schema support' >&2
    exit 1
fi
grep -F 'static pointer initializer requires a null or zero-addend object address constant' \
    "$work/invalid-addend.stderr" >/dev/null

if "$minic" -S "$root/tests/compiler/c0/invalid_static_pointer_subscript_relocation.c" \
    -o "$work/invalid-pointer-subscript.s" \
    >"$work/invalid-pointer-subscript.stdout" 2>"$work/invalid-pointer-subscript.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/static-object-address: runtime pointer subscript accepted as link-time relocation' >&2
    exit 1
fi
grep -F 'static pointer initializer requires a null or zero-addend object address constant' \
    "$work/invalid-pointer-subscript.stderr" >/dev/null

printf '%s\n' 'PASS compiler/c0/static-object-address relocation=scalar-global-address+zero-offset-array-decay+string-literal cast+direct+parenthesized null=shared addend=fail-closed pointer-subscript=fail-closed type=checked'
