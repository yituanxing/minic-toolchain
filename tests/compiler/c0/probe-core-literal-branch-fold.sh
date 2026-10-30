#!/usr/bin/env bash
set -Eeuo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
"${HOST_CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror \
    -ffunction-sections -fdata-sections -Wl,--gc-sections -I"$root/src" \
    "$root/tests/compiler/c0/probe-core-literal-branch-fold.c" \
    "$root/src/core/core_ir.c" "$root/src/frontend/type.c" \
    -o "$work/core-literal-branch-fold"
"$work/core-literal-branch-fold"
