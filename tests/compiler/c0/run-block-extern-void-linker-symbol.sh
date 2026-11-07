#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-block-extern-void-linker-symbol

mkdir -p "$work"
"$host_cc" -std=gnu11 -Wall -Wextra -fsyntax-only \
    "$root/tests/compiler/c0/block_extern_void_linker_symbol.c"
"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/block_extern_void_linker_symbol.c" -o "$work/good.i"
MINIC_CORE_IR=strict "$minic_cc" --target=x86_64-linux -S \
    "$work/good.i" -o "$work/x86.s"

for target in riscv32-linux riscv64-linux aarch64-linux loongarch64-linux; do
    "$minic_cc" --target="$target" -S "$work/good.i" -o "$work/$target.s"
done

if "$host_cc" -std=gnu11 -pedantic-errors -fsyntax-only \
    "$root/tests/compiler/c0/invalid_void_object_definition.c" \
    >"$work/negative.gnu.stdout" 2>"$work/negative.gnu.stderr"; then
    echo 'FAIL GCC accepted a void object definition' >&2
    exit 1
fi
"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/invalid_void_object_definition.c" -o "$work/negative.i"
if "$minic_cc" --target=x86_64-linux -S "$work/negative.i" -o "$work/negative.s" \
    >"$work/negative.stdout" 2>"$work/negative.stderr"; then
    echo 'FAIL MiniC accepted a void object definition' >&2
    exit 1
fi

printf '%s\n' 'PASS compiler/c0/block-extern-void-linker-symbol GNU-weak-void-extern=1 void-definition-rejected=1 five-targets=1 strict-core=1 assembly-only=1'
