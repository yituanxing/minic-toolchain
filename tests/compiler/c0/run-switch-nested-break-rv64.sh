#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
riscv_cc=${RISCV_CC:-riscv64-linux-gnu-gcc}
qemu=${QEMU_RISCV64:-qemu-riscv64}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-switch-nested-break-rv64
source="$root/tests/compiler/c0/switch_nested_break_rv64.c"

rm -rf "$work"
mkdir -p "$work"
"$riscv_cc" -E -P -std=gnu11 -x c "$source" -o "$work/probe.i"
"$minic" -S "$work/probe.i" -o "$work/minic.s"
"$riscv_cc" -static "$work/minic.s" -o "$work/minic.elf"
"$riscv_cc" -static -std=gnu11 "$source" -o "$work/gcc.elf"

set +e
timeout 3s "$qemu" "$work/gcc.elf"
gcc_status=$?
timeout 3s "$qemu" "$work/minic.elf"
minic_status=$?
set -e

if test "$gcc_status" -ne 0 || test "$minic_status" -ne "$gcc_status"; then
    printf '%s\n' "FAIL compiler/c0/switch_nested_break_rv64 gcc=$gcc_status minic=$minic_status" >&2
    exit 1
fi
printf '%s\n' 'PASS compiler/c0/switch_nested_break_rv64 macro-block=1 nested-if-break=1 post-switch-reachable=1 qemu=1'
