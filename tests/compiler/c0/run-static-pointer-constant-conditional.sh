#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/static-pointer-constant-conditional
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/static_pointer_constant_conditional.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
grep -F 'selected:' "$work/output.s" >/dev/null
grep -F 'null_selected:' "$work/output.s" >/dev/null
grep -F '  .dword value' "$work/output.s" >/dev/null
grep -F '  .dword 0' "$work/output.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/static-pointer-constant-conditional symbol+null selected-by-ICE'
