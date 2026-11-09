#!/usr/bin/env bash
set -Eeuo pipefail

host_cc=${HOST_CC:-cc}
root=$(cd "$(dirname "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

sources=(
    "$root/tests/compiler/c0/probe-core-numeric-coretypes.c"
    "$root/src/core/core_type.c"
    "$root/src/core/core_ir_verify.c"
    "$root/src/target/common/core_numeric_shape.c"
    "$root/src/target/common/direct_instruction_semantics.c"
    "$root/src/target/common/lp64_scalar.c"
    "$root/src/frontend/type.c"
    "$root/src/frontend/ast.c"
    "$root/src/target/data_layout.c"
    "$root/src/target/source_policy/data_layout_source.c"
    "$root/src/target/common/lp64_aggregate.c"
    "$root/src/target/common/aggregate_shape.c"
)

for mode in native asan; do
    extra=(-O2)
    if [[ "$mode" == asan ]]; then
        extra=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
    fi
    "$host_cc" -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror \
        -ffunction-sections -fdata-sections -Wl,--gc-sections \
        -I"$root/include" -I"$root/src" "${extra[@]}" \
        "${sources[@]}" -o "$work/numeric-$mode"
    ASAN_OPTIONS=detect_leaks=0 "$work/numeric-$mode"
done

echo 'CORE_NUMERIC_CORETYPE_GATE=PASS native=1 ASAN_UBSAN=1 source_layout_query=0'
