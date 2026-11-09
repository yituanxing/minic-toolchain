#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/core-record-call-conditional-subscript
mkdir -p "$work"

"$host_cc" -std=c11 -E -P -x c \
    "$root/tests/compiler/c0/core_record_call_conditional_subscript.c" \
    -o "$work/input.i"
MINIC_CORE_IR=strict "$minic_cc" --target=x86_64-linux \
    -S "$work/input.i" -o "$work/x86_64.s"
"$minic_cc" --target=riscv64-linux -S "$work/input.i" -o "$work/riscv64.s"
"$minic_cc" --target=riscv32-linux -S "$work/input.i" -o "$work/riscv32.s"
"$minic_cc" --target=aarch64-linux -S "$work/input.i" -o "$work/aarch64.s"
"$minic_cc" --target=loongarch64-linux -S "$work/input.i" -o "$work/loongarch64.s"
"$host_cc" -no-pie "$work/x86_64.s" -o "$work/minic-runtime"
"$host_cc" -std=c11 -no-pie "$work/input.i" -o "$work/host-runtime"
"$work/minic-runtime"
"$work/host-runtime"

printf '%s\n' 'PASS compiler/c0/core_record_call_conditional_subscript strict-core=1 targets=x86_64,riscv32,riscv64,aarch64,loongarch64 runtime=x86_64 24-byte-record=1 rhs-once=1 both-ternary-arms=1'
