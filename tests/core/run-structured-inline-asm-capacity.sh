#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/core-structured-inline-asm-capacity
mkdir -p "$work"
"$host_cc" -std=c11 -O2 -Wall -Wextra -Werror \
    -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -I"$root/src" \
    "$root/src/core/core_ir.c" \
    "$root/src/core/core_ir_verify.c" \
    "$root/src/core/core_lower_asm_operands.c" \
    "$root/src/frontend/type.c" \
    "$root/tests/core/structured_inline_asm_capacity_test.c" \
    -o "$work/core-structured-inline-asm-capacity"
"$work/core-structured-inline-asm-capacity"
