#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-deferred-declarator-attributes

rm -rf "$work"
mkdir -p "$work"

"$minic" -S "$root/tests/compiler/c0/deferred_declarator_attributes.c" \
    -o "$work/deferred_declarator_attributes.s"
test -s "$work/deferred_declarator_attributes.s"
grep -F '.section .text.preptr' "$work/deferred_declarator_attributes.s" >/dev/null
grep -F 'map_before_pointer:' "$work/deferred_declarator_attributes.s" >/dev/null
grep -F 'map_after_pointer:' "$work/deferred_declarator_attributes.s" >/dev/null
grep -F 'call map_before_pointer' "$work/deferred_declarator_attributes.s" >/dev/null
grep -F 'call map_after_pointer' "$work/deferred_declarator_attributes.s" >/dev/null
grep -F 'kfree_skb_reason_shape:' "$work/deferred_declarator_attributes.s" >/dev/null
grep -F 'noclone_after_return_pointer:' "$work/deferred_declarator_attributes.s" >/dev/null

if "$minic" -S "$root/tests/compiler/c0/invalid_function_attribute_on_pointer_object.c" \
    -o "$work/invalid-object.s" >"$work/invalid-object.stdout" 2>"$work/invalid-object.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/deferred_declarator_attributes: function-only attribute leaked onto object' >&2
    exit 1
fi
grep -F 'unsupported GNU object attribute' "$work/invalid-object.stderr" >/dev/null

if "$minic" -S "$root/tests/compiler/c0/invalid_noinline_argument.c" \
    -o "$work/invalid-noinline-argument.s" >"$work/invalid-noinline-argument.stdout" 2>"$work/invalid-noinline-argument.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/deferred_declarator_attributes: noinline accepted an argument' >&2
    exit 1
fi
grep -F 'GNU attribute has an invalid number of arguments' "$work/invalid-noinline-argument.stderr" >/dev/null

if "$minic" -S "$root/tests/compiler/c0/invalid_noclone_argument.c" \
    -o "$work/invalid-noclone-argument.s" >"$work/invalid-noclone-argument.stdout" 2>"$work/invalid-noclone-argument.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/deferred_declarator_attributes: noclone accepted an argument' >&2
    exit 1
fi
grep -F 'GNU attribute has an invalid number of arguments' "$work/invalid-noclone-argument.stderr" >/dev/null

if "$minic" -S "$root/tests/compiler/c0/invalid_noclone_object.c" \
    -o "$work/invalid-noclone-object.s" >"$work/invalid-noclone-object.stdout" 2>"$work/invalid-noclone-object.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/deferred_declarator_attributes: noclone leaked onto object' >&2
    exit 1
fi
grep -F 'unsupported GNU object attribute' "$work/invalid-noclone-object.stderr" >/dev/null

printf '%s\n' 'PASS compiler/c0/deferred_declarator_attributes pre-pointer=generic post-pointer=generic function-target=late object-target=late section=preserved noinline=parse-only noclone=parse-only+function-only+zero-arg'
