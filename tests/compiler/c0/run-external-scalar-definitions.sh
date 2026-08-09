#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-external-scalar-definitions

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/external_scalar_definition.c" \
    -o "$work/external_scalar_definition.i"
"$minic" -S "$work/external_scalar_definition.i" \
    -o "$work/external_scalar_definition.s"

grep -F '.globl external_count' "$work/external_scalar_definition.s" >/dev/null
grep -F 'external_count:' "$work/external_scalar_definition.s" >/dev/null
grep -F '  .word 7' "$work/external_scalar_definition.s" >/dev/null
grep -F '.globl external_wide' "$work/external_scalar_definition.s" >/dev/null
grep -F '  .dword 11' "$work/external_scalar_definition.s" >/dev/null
test "$(grep -c '^external_count:$' "$work/external_scalar_definition.s")" -eq 1
printf '%s\n' 'PASS compiler/c0/external_scalar_definition extern-merge=1 int=.word long-long=.dword linkage=external'
