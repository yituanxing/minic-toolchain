#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
riscv_cc=${RISCV_CC:-riscv64-linux-gnu-gcc}
qemu=${QEMU_RISCV64:-qemu-riscv64}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-stack-fixed-arguments-rv64

rm -rf "$work"
mkdir -p "$work"

"$riscv_cc" -E -P -x c \
    "$root/tests/compiler/c0/stack_fixed_arguments.c" \
    -o "$work/stack_fixed_arguments.i"
"$minic" -S \
    "$work/stack_fixed_arguments.i" \
    -o "$work/stack_fixed_arguments.s"
"$riscv_cc" -static \
    "$work/stack_fixed_arguments.s" \
    "$root/tests/compiler/c0/stack_fixed_arguments_gcc.c" \
    -o "$work/stack_fixed_arguments.elf"

set +e
"$qemu" "$work/stack_fixed_arguments.elf"
status=$?
set -e
if test "$status" -ne 0; then
    printf '%s\n' "FAIL compiler/c0/stack_fixed_arguments_rv64 exit=$status" >&2
    exit 1
fi
printf '%s\n' 'PASS compiler/c0/stack_fixed_arguments_rv64 callers=minic,gcc callees=gcc,minic fixed=9 stack-slot=1'
