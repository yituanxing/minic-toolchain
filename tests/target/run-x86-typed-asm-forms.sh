#!/usr/bin/env bash
set -Eeuo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
temp="$(mktemp -d)"
trap 'rm -rf "$temp"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Wpedantic -Wconversion -Wshadow \
    -Werror -ffunction-sections -fdata-sections \
    -I"$root/include" -I"$root/src" \
    "$root/tests/target/x86_64_typed_asm_form_test.c" \
    "$root/src/target/x86_64/asm_contract.c" \
    -Wl,--gc-sections -o "$temp/form-test"
"$temp/form-test"
