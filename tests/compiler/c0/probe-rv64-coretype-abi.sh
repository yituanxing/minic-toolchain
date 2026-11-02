#!/usr/bin/env bash
set -Eeuo pipefail
repo=$(CDPATH= cd -- "$(dirname "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

sources=(
    "$repo/tests/compiler/c0/probe-rv64-coretype-abi.c"
    "$repo/src/core/core_type.c"
    "$repo/src/target/common/aggregate_shape.c"
    "$repo/src/target/riscv64/abi.c"
)
for mode in native asan; do
    extra=(-O2)
    if [[ "$mode" == asan ]]; then
        extra=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
    fi
    "${HOST_CC:-cc}" -std=c11 -Wall -Wextra -Wpedantic -Wconversion \
        -Wshadow -Werror -ffunction-sections -fdata-sections \
        -Wl,--gc-sections -I"$repo/include" -I"$repo/src" \
        "${extra[@]}" "${sources[@]}" -o "$work/core-abi-$mode"
    ASAN_OPTIONS=detect_leaks=0 "$work/core-abi-$mode"
done
