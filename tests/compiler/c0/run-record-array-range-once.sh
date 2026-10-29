#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-record-array-range-once

mkdir -p "$work"
"$host_cc" -std=gnu11 -E -P -x c \
    "$root/tests/compiler/c0/record_array_range_once.c" -o "$work/input.i"
MINIC_CORE_IR=strict "$minic_cc" --target=x86_64-linux -S \
    "$work/input.i" -o "$work/x86.s"
"$minic_cc" --target=aarch64-linux -S "$work/input.i" -o "$work/aarch64.s"
"$minic_cc" --target=loongarch64-linux -S "$work/input.i" -o "$work/loong.s"
"$host_cc" -no-pie "$work/x86.s" -o "$work/x86"
"$work/x86"

printf '%s\n' 'PASS compiler/c0/record_array_range_once aggregate-range=1 once-only-call=1 zero-gaps=1 compound-literal=1 x86-native=1 other-targets=2 strict-core=1'
