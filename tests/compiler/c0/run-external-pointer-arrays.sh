#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-external-pointer-array
asm="$work/external_pointer_array.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/external_pointer_array.c" \
    -o "$work/external_pointer_array.i"
"$minic" -S "$work/external_pointer_array.i" -o "$asm"

grep -F '.type names, @object' "$asm" >/dev/null
grep -F '.globl names' "$asm" >/dev/null
grep -F '  .dword shared_name' "$asm" >/dev/null
grep -F '  .dword .Lminic_string_' "$asm" >/dev/null
grep -F '  .dword 0' "$asm" >/dev/null
grep -F '.size names, 32' "$asm" >/dev/null
grep -F '.type envp_init, @object' "$asm" >/dev/null
grep -F '.globl envp_init' "$asm" >/dev/null
grep -F '  .zero 248' "$asm" >/dev/null
grep -F '.size envp_init, 272' "$asm" >/dev/null
printf '%s\n' 'PASS compiler/c0/external_pointer_array bound=4 string-reloc=2 object-reloc=1 null=1 envp-tail-zero=31 storage=32,272'
