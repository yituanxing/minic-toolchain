#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-mixed-symbol-relocations

rm -rf "$work"
mkdir -p "$work"

"$minic" -S "$root/tests/compiler/c0/static_mixed_symbol_relocations.c" \
    -o "$work/static_mixed_symbol_relocations.s"
test -s "$work/static_mixed_symbol_relocations.s"
grep -F 'entry:' "$work/static_mixed_symbol_relocations.s" >/dev/null
grep -F '.dword setup_name' "$work/static_mixed_symbol_relocations.s" >/dev/null
grep -F '.dword setup_fn' "$work/static_mixed_symbol_relocations.s" >/dev/null

if "$minic" -S "$root/tests/compiler/c0/invalid_static_record_object_relocation_type.c" \
    -o "$work/invalid-type.s" >"$work/invalid-type.stdout" 2>"$work/invalid-type.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/static-mixed-symbol-relocations: incompatible record pointer field accepted' >&2
    exit 1
fi
grep -F 'static record pointer initializer type mismatch' "$work/invalid-type.stderr" >/dev/null

printf '%s\n' 'PASS compiler/c0/static-mixed-symbol-relocations location=storage-byte-offset target=object+function mixed-record=accepted type=checked'
