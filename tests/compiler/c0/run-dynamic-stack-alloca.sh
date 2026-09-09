#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
riscv_cc=${RISCV_CC:-riscv64-linux-gnu-gcc}
qemu=${QEMU_RISCV64:-qemu-riscv64}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0/dynamic-stack-alloca

rm -rf "$work"
mkdir -p "$work"

"$minic" -S "$root/tests/compiler/c0/dynamic_stack_alloca.c" -o "$work/minic.s"
test -s "$work/minic.s"
"$riscv_cc" -O0 -static     "$work/minic.s"     "$root/tests/compiler/c0/dynamic_stack_alloca_helper.c"     -o "$work/minic.elf"
"$qemu" "$work/minic.elf"

printf '%s\n' 'PASS compiler/c0/dynamic_stack_alloca nested=2 fixed-frame=1 outgoing-stack-args=2 runtime=qemu'
