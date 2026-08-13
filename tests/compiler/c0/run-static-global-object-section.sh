#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-global-object-section

rm -rf "$work"
mkdir -p "$work"

"$minic" -S "$root/tests/compiler/c0/static_global_object_section.c" \
    -o "$work/static_global_object_section.s"
test -s "$work/static_global_object_section.s"
grep -F '.section .data.static.init' "$work/static_global_object_section.s" >/dev/null
grep -F 'section_initialized:' "$work/static_global_object_section.s" >/dev/null
grep -F '.section .data.static.zero' "$work/static_global_object_section.s" >/dev/null
grep -F 'section_zero:' "$work/static_global_object_section.s" >/dev/null
grep -F '.section .discard.addressable' "$work/static_global_object_section.s" >/dev/null
grep -F 'addressable_shape:' "$work/static_global_object_section.s" >/dev/null

if "$minic" -S "$root/tests/compiler/c0/invalid_static_global_alignment.c" \
    -o "$work/invalid-alignment.s" >"$work/invalid-alignment.stdout" 2>"$work/invalid-alignment.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/static-global-object-section: static alignment widened accidentally' >&2
    exit 1
fi
grep -F 'static object symbol/layout attributes require explicit object semantics' \
    "$work/invalid-alignment.stderr" >/dev/null

printf '%s\n' 'PASS compiler/c0/static-global-object-section section=global-metadata initialized+zero+used-composition alignment=fail-closed'
