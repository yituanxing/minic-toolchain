#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/three-linux-compiler/bin/minic-cc"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/three-linux-compiler"}/tests/gnu-numeric-pointer-global
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/gnu_numeric_pointer_global.c" \
    -o "$work/pointer.i"

for target in x86_64-linux aarch64-linux loongarch64-linux riscv64-linux riscv32-linux; do
    "$minic" --target="$target" -S "$work/pointer.i" -o "$work/$target.s"
    grep -F 'empty_list:' "$work/$target.s" >/dev/null
    grep -F 'guard_page:' "$work/$target.s" >/dev/null
    if [ "$target" = aarch64-linux ]; then
        grep -F '  cset x9, ne' "$work/$target.s" >/dev/null
    elif [ "$target" = loongarch64-linux ]; then
        grep -F '  sltu $t0, $zero, $t0' "$work/$target.s" >/dev/null
    fi
    if [ "$target" = riscv32-linux ]; then
        grep -E '^  \.(4byte|word) 4294967295$' "$work/$target.s" >/dev/null
        grep -E '^  \.(4byte|word) 4096$' "$work/$target.s" >/dev/null
    elif [ "$target" = riscv64-linux ]; then
        grep -F '  .dword -1' "$work/$target.s" >/dev/null
        grep -F '  .dword 4096' "$work/$target.s" >/dev/null
    else
        grep -E '^  \.(8byte|quad|dword) 18446744073709551615$' "$work/$target.s" >/dev/null
        grep -E '^  \.(8byte|quad|dword) 4096$' "$work/$target.s" >/dev/null
    fi
    printf 'PASS GNU numeric pointer globals: %s\n' "$target"
done
