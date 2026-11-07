#!/usr/bin/env bash
set -Eeuo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

${HOST_CC:-cc} -E -P -std=gnu11 -x c \
  "$root/tests/compiler/c0/core_asm_nonvolatile_rk_output.c" \
  -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
grep -Eq 'addi.*zero.*0' "$work/output.s"
echo 'CORE_ASM_NONVOLATILE_RK=PASS output_is_execution_effect=1 immediate=0'
