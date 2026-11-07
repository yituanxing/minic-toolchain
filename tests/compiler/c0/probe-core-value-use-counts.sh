#!/usr/bin/env bash
set -Eeuo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
"${HOST_CC:-cc}" -std=c11 -Wall -Wextra -Werror -I"$root/src" \
    "$root/tests/compiler/c0/probe-core-value-use-counts.c" \
    "$root/src/core/value_uses.c" -o "$work/core-value-use-counts"
"$work/core-value-use-counts"
