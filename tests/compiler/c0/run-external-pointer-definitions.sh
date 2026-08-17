#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
riscv_cc=${RISCV_CC:-riscv64-linux-gnu-gcc}
qemu_riscv64=${QEMU_RISCV64:-qemu-riscv64}
require_runtime=${REQUIRE_RISCV_RUNTIME:-0}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-external-pointer-definitions
rm -rf "$work" && mkdir -p "$work"
"$host_cc" -E -P -x c "$root/tests/compiler/c0/external_pointer_definition.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
for symbol in message power_off object_pointer member_pointer callback_pointer; do
    grep -F ".globl $symbol" "$work/output.s" >/dev/null
    grep -F "$symbol:" "$work/output.s" >/dev/null
done
grep -E '^  \.dword \.Lminic_string_[0-9]+$' "$work/output.s" >/dev/null
grep -F '  .dword object_target' "$work/output.s" >/dev/null
grep -E '^  \.dword pair_target([+-][0-9]+)?$' "$work/output.s" >/dev/null
grep -F '  .dword callback_target' "$work/output.s" >/dev/null
if command -v "$riscv_cc" >/dev/null 2>&1 && command -v "$qemu_riscv64" >/dev/null 2>&1; then
    "$riscv_cc" -static "$work/output.s" -o "$work/minic-runtime"
    "$riscv_cc" -std=gnu11 -static "$root/tests/compiler/c0/external_pointer_definition.c" -o "$work/gcc-runtime"
    set +e
    "$qemu_riscv64" "$work/gcc-runtime"; gcc_status=$?
    "$qemu_riscv64" "$work/minic-runtime"; minic_status=$?
    set -e
    test "$gcc_status" -eq 0
    test "$minic_status" -eq "$gcc_status"
elif test "$require_runtime" = 1; then
    exit 1
fi
printf '%s\n' 'PASS compiler/c0/external_pointer_definition linkage=external initializer=string|null|object-address|member-address|function-address owner=shared-pointer-object'
