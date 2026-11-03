#!/usr/bin/env bash
set -Eeuo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
host_cc=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

"$host_cc" -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Wstrict-prototypes -Wmissing-prototypes -Werror -O2 \
    -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -I"$root/src" -I"$root/include" \
    "$root/src/core/core_ir.c" \
    "$root/src/core/core_type.c" \
    "$root/tests/target/riscv/coretype_root_alignment_test.c" \
    -o "$work/riscv-coretype-root-alignment"
"$work/riscv-coretype-root-alignment"
