#!/usr/bin/env bash
set -Eeuo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

${HOST_CC:-cc} -E -P -std=gnu11 -x c \
  "$root/tests/compiler/c0/core_asm_zero_stride_immediate.c" \
  -o "$work/input.i"
if "$minic" -S "$work/input.i" -o "$work/output.s" \
    >"$work/stdout" 2>"$work/stderr"; then
    echo 'CORE_ASM_ZERO_STRIDE=FAIL invalid symbolic offset was accepted' >&2
    exit 1
fi
if grep -Ei 'floating point exception|division by zero|core dumped' \
    "$work/stderr" >/dev/null; then
    echo 'CORE_ASM_ZERO_STRIDE=FAIL compiler crashed in zero-stride arithmetic' >&2
    exit 1
fi
test ! -s "$work/output.s"
echo 'CORE_ASM_ZERO_STRIDE=PASS rejects_invalid_zero_stride_without_crash=1'
