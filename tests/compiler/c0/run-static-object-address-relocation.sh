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
test "$count" -eq 3
array_count=$(grep -F -c '.dword global_address_array' "$work/static_object_address_relocation.s")
test "$array_count" -eq 4
grep -F '.dword global_address_array+4' "$work/static_object_address_relocation.s" >/dev/null
grep -F '.dword global_address_array+36' "$work/static_object_address_relocation.s" >/dev/null
grep -F '.dword internal_address_target+4' "$work/static_object_address_relocation.s" >/dev/null
pointer_sign_count=$(grep -F -c '.dword pointer_sign_ids' "$work/static_object_address_relocation.s")
test "$pointer_sign_count" -eq 2
grep -F 'string_literal_address:' "$work/static_object_address_relocation.s" >/dev/null
grep -F '.dword .Lminic_string_' "$work/static_object_address_relocation.s" >/dev/null
function_count=$(grep -F -c '.dword function_address_target' "$work/static_object_address_relocation.s")
test "$function_count" -eq 2
external_count=$(grep -F -c '.dword external_address_target' "$work/static_object_address_relocation.s")
test "$external_count" -eq 2
grep -F '.dword subobject_address_target+4' "$work/static_object_address_relocation.s" >/dev/null
grep -F '.dword subobject_address_target+12' "$work/static_object_address_relocation.s" >/dev/null
grep -F '.dword subobject_address_target+21' "$work/static_object_address_relocation.s" >/dev/null
grep -F '.dword subobject_address_target+24' "$work/static_object_address_relocation.s" >/dev/null
grep -F '.dword -1' "$work/static_object_address_relocation.s" >/dev/null
grep -F '.dword 4294967295' "$work/static_object_address_relocation.s" >/dev/null
high_pointer_count=$(grep -F -c '.dword -2401263026318605568' "$work/static_object_address_relocation.s")
test "$high_pointer_count" -eq 2
grep -F '.dword 4108' "$work/static_object_address_relocation.s" >/dev/null
grep -F '.dword 4088' "$work/static_object_address_relocation.s" >/dev/null
scaled_add_count=$(grep -F -c '.dword 4108' "$work/static_object_address_relocation.s")
test "$scaled_add_count" -eq 2

if "$minic" -S "$root/tests/compiler/c0/invalid_static_object_address_type.c" \
    -o "$work/invalid-type.s" >"$work/invalid-type.stdout" 2>"$work/invalid-type.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/static-object-address: incompatible pointer type accepted' >&2
    exit 1
fi
grep -F 'static pointer initializer type mismatch' "$work/invalid-type.stderr" >/dev/null

if "$minic" -S "$root/tests/compiler/c0/invalid_static_pointer_sign_qualifier_loss.c" \
    -o "$work/invalid-pointer-sign-qualifier.s" \
    >"$work/invalid-pointer-sign-qualifier.stdout" \
    2>"$work/invalid-pointer-sign-qualifier.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/static-object-address: pointer-sign qualifier loss accepted' >&2
    exit 1
fi
grep -F 'static pointer initializer type mismatch' \
    "$work/invalid-pointer-sign-qualifier.stderr" >/dev/null

if "$minic" -S "$root/tests/compiler/c0/invalid_static_pointer_arithmetic_base.c" \
    -o "$work/invalid-pointer-arithmetic.s" \
    >"$work/invalid-pointer-arithmetic.stdout" 2>"$work/invalid-pointer-arithmetic.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/static-object-address: runtime pointer base accepted as static arithmetic' >&2
    exit 1
fi
grep -F 'static pointer initializer requires a null or static symbol address constant' \
    "$work/invalid-pointer-arithmetic.stderr" >/dev/null

if "$minic" -S "$root/tests/compiler/c0/invalid_static_pointer_subscript_relocation.c" \
    -o "$work/invalid-pointer-subscript.s" \
    >"$work/invalid-pointer-subscript.stdout" 2>"$work/invalid-pointer-subscript.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/static-object-address: runtime pointer subscript accepted as link-time relocation' >&2
    exit 1
fi
grep -F 'static pointer initializer requires a null or static symbol address constant' \
    "$work/invalid-pointer-subscript.stderr" >/dev/null

printf '%s\n' 'PASS compiler/c0/static-object-address relocation=symbolic-object+function explicit-pointer-cast=preserved scalar+aggregate=1 zero-offset-array-decay+string-literal pointer-sign=static-init-only pointer-sign-qualifier-loss=reject null=shared addend=signed-static pointer-subscript=fail-closed type=checked'
