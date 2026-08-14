#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-local-pointer-array
asm="$work/static_local_pointer_array.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/static_local_pointer_array.c" \
    -o "$work/static_local_pointer_array.i"
"$minic" -S "$work/static_local_pointer_array.i" -o "$asm"

grep -F '.type __minic_static_local_' "$asm" >/dev/null
if grep -F '.globl __minic_static_local_' "$asm" >/dev/null; then
    echo 'static local pointer array leaked external linkage' >&2
    exit 1
fi
grep -F '  .dword shared_event' "$asm" >/dev/null
grep -F '  .dword .Lminic_string_' "$asm" >/dev/null
grep -F '  .zero 8' "$asm" >/dev/null
grep -E '^\.size __minic_static_local_.*,[[:space:]]*32$' "$asm" >/dev/null
printf '%s\n' 'PASS compiler/c0/static_local_pointer_array inferred=4 string-reloc=2 object-reloc=1 null=1 internal=1'
