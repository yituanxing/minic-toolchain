#!/usr/bin/env bash
set -Eeuo pipefail

host_cc=${1:-cc}
root=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

sources=(
    "$root/tests/core/core_type_verifier_test.c"
    "$root/src/core/core_ir.c"
    "$root/src/compiler/c_lower/core_ir_source.c"
    "$root/src/core/core_type.c"
    "$root/src/core/core_ir_verify.c"
    "$root/src/frontend/type.c"
)

for mode in native asan; do
    if [ "$mode" = asan ]; then
        extra=(-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer)
    else
        extra=(-O2)
    fi
    "$host_cc" -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
        -Wstrict-prototypes -Wmissing-prototypes -Werror \
        -I"$root/src" -I"$root/include" "${extra[@]}" \
        "${sources[@]}" -o "$work/core-type-verifier-$mode"
    ASAN_OPTIONS=detect_leaks=0 "$work/core-type-verifier-$mode"
done

echo 'CORE_TYPE_VERIFIER_SANITIZERS=PASS native=1 ASAN_UBSAN=1'
