#!/usr/bin/env bash
set -Eeuo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
"${HOST_CC:-cc}" -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror \
    -ffunction-sections -fdata-sections -Wl,--gc-sections -I"$root/src" \
    "$root/tests/core/core_call_dump_test.c" \
    "$root/src/core/core_ir.c" "$root/src/core/core_ir_verify.c" \
    "$root/src/core/core_ir_dump.c" "$root/src/frontend/type.c" \
    -o "$work/core-call-dump"
"$work/core-call-dump"
