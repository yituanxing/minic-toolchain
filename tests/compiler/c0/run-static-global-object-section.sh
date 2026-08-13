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
grep -F '.section .init.rodata' "$work/static_global_object_section.s" >/dev/null
grep -F 'linux_setup_string:' "$work/static_global_object_section.s" >/dev/null
grep -F 'aligned_static:' "$work/static_global_object_section.s" >/dev/null
awk '
    /[.]type aligned_static, @object/ { seen = 1; next }
    seen && /[.]align 4/ { aligned = 1; next }
    seen && /aligned_static:/ { exit(aligned ? 0 : 1) }
    END { if (!seen || !aligned) exit 1 }
' "$work/static_global_object_section.s"

if "$minic" -S "$root/tests/compiler/c0/invalid_static_global_alignment.c" \
    -o "$work/invalid-alignment.s" >"$work/invalid-alignment.stdout" 2>"$work/invalid-alignment.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/static-global-object-section: non-power-of-two static alignment accepted' >&2
    exit 1
fi
grep -F 'GNU object alignment must be a power of two' "$work/invalid-alignment.stderr" >/dev/null

printf '%s\n' 'PASS compiler/c0/static-global-object-section section=global-metadata initialized+zero+used+post-array alignment=global-metadata+validated inferred-char-array=string-bound'
