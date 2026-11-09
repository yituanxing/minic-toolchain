#!/usr/bin/env bash
set -Eeuo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

${HOST_CC:-cc} -E -P -std=gnu11 -x c \
  "$root/tests/compiler/c0/core_asm_empty_rk_immediate.c" \
  -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
test -s "$work/output.s"
echo 'CORE_ASM_EMPTY_RK=PASS zero_template_constant_input=compiler_barrier'
