#!/usr/bin/env bash
set -Eeuo pipefail

host_cc=${1:-cc}
root=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

sources=(
    "$root/tests/core/core_ir_shape_test.c"
    "$root/src/core/core_ir.c"
    "$root/src/compiler/c_lower/core_ir_source.c"
    "$root/src/core/core_type.c"
    "$root/src/core/core_ir_verify.c"
    "$root/src/frontend/type.c"
)
cases=(
    fixed fixed-null fixed-overcapacity
    symbol symbol-null symbol-overcapacity
    enum enum-pointer enum-null enum-overcapacity enum-missing
    variadic
)

for mode in native asan; do
    extra=()
    if [ "$mode" = asan ]; then
        extra=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
    else
        extra=(-O2)
    fi
    "$host_cc" -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror \
        -I"$root/src" -I"$root/include" "${extra[@]}" \
        "${sources[@]}" -o "$work/core-ir-shapes-$mode"
    "$host_cc" -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror \
        -ffunction-sections -fdata-sections -Wl,--gc-sections \
        -I"$root/src" -I"$root/include" "${extra[@]}" \
        "$root/tests/core/core_block_address_reachability_test.c" \
        "$root/src/core/core_ir.c" -o "$work/core-block-address-$mode"
    for case_name in "${cases[@]}"; do
        # The worker execution environment may run under ptrace, where
        # LeakSanitizer itself is unavailable. ASan/UBSan stay enabled.
        ASAN_OPTIONS=detect_leaks=0 "$work/core-ir-shapes-$mode" "$case_name"
    done
    ASAN_OPTIONS=detect_leaks=0 "$work/core-block-address-$mode"
done

echo 'CORE_IR_SHAPES=PASS valid=5 malformed=7 native=12 ASAN_UBSAN=12 Core_BLOCK_ADDRESS=2 C_producer_claim=none'
