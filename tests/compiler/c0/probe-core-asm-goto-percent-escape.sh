#!/usr/bin/env bash
# GNU escaped percents are not operands, including %%0 adjacent to a live %0.
set -Eeuo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

${HOST_CC:-cc} -E -P -std=gnu11 -x c \
  "$root/tests/compiler/c0/core_asm_goto_escaped_percent.c" \
  -o "$work/fixture.i"
"$minic" -S "$work/fixture.i" -o "$work/fixture.s"

grep -Fq '.ascii "%0"' "$work/fixture.s"
grep -Fq '.word 33' "$work/fixture.s"
if grep -Fq '.ascii "%33"' "$work/fixture.s"; then
    echo 'CORE_ASM_GOTO_PERCENT_ESCAPE=FAIL literal-percent misidentified as operand' >&2
    exit 1
fi
echo 'CORE_ASM_GOTO_PERCENT_ESCAPE=PASS literal_%%0=preserved integer_%0=33'
