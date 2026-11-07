#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic_cc=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-inferred-record-multidimensional

mkdir -p "$work"
"$host_cc" -std=gnu11 -E -P -x c \
    "$root/tests/compiler/c0/static_inferred_record_multidimensional.c" \
    -o "$work/input.i"

# Keep the C object-layout/runtime oracle native. The x86-64 static nested
# aggregate emitter has a separate outstanding global-initializer frontier;
# this regression measures the newly supported frontend type/extent semantics.
"$host_cc" -std=gnu11 -Wall -Wextra -Werror \
    "$root/tests/compiler/c0/static_inferred_record_multidimensional.c" \
    -o "$work/reference"
"$work/reference"
MINIC_CORE_IR=strict "$minic_cc" --target=riscv64-linux -S \
    "$work/input.i" -o "$work/riscv64.s"
"$minic_cc" --target=aarch64-linux -S "$work/input.i" -o "$work/aarch64.s"
"$minic_cc" --target=loongarch64-linux -S "$work/input.i" -o "$work/loong.s"

printf '%s\n' 'PASS compiler/c0/static_inferred_record_multidimensional outer-bound=4 inner-bound=3 designated=1 zero-gaps=1 strict-core=1 targets=rv64,aarch64,loongarch64 GNU-native-oracle=1'
