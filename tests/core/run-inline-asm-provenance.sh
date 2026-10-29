#!/usr/bin/env bash
set -Eeuo pipefail

root=$(cd "$(dirname "$0")/../.." && pwd)
host_cc=${CC:-cc}
work=${BUILD_DIR:-"$root/build/debug"}/tests/core-inline-asm-provenance
mkdir -p "$work"
"$host_cc" -std=c11 -O2 -Wall -Wextra -Werror \
    -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -I"$root/src" \
    "$root/src/core/core_ir.c" \
    "$root/src/frontend/type.c" \
    "$root/tests/core/inline_asm_provenance_test.c" \
    -o "$work/core-inline-asm-provenance"
"$work/core-inline-asm-provenance"
