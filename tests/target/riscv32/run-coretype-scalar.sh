#!/usr/bin/env bash
set -Eeuo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
host_cc=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

for mode in native sanitize; do
    flags=(-O2)
    if [[ $mode == sanitize ]]; then
        flags=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
    fi
    "$host_cc" -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror \
        -ffunction-sections -fdata-sections -Wl,--gc-sections \
        -I"$root/src" -I"$root/include" "${flags[@]}" \
        "$root/src/core/core_type.c" \
        "$root/src/target/riscv32/rv32_scalar.c" \
        "$root/src/target/riscv32/rv32_wide_emit.c" \
        "$root/tests/target/riscv32/coretype_scalar_test.c" \
        -o "$work/rv32-coretype-$mode"
    ASAN_OPTIONS=detect_leaks=0 "$work/rv32-coretype-$mode"
done
