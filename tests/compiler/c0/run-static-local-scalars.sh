#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-local-scalar

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/static_local_scalar.c" \
    -o "$work/static_local_scalar.i"
"$minic" -S "$work/static_local_scalar.i" \
    -o "$work/static_local_scalar.s"

grep -Fx '.data' "$work/static_local_scalar.s" >/dev/null
grep -Fx '.section .rodata' "$work/static_local_scalar.s" >/dev/null
test "$(grep -c -F '.type __minic_static_local_' "$work/static_local_scalar.s")" -eq 2
test "$(grep -c -F '  .zero 8' "$work/static_local_scalar.s")" -ge 2
if grep -F '.globl __minic_static_local_' "$work/static_local_scalar.s" >/dev/null; then
    echo 'static local scalar leaked external linkage' >&2
    exit 1
fi
printf '%s\n' 'PASS compiler/c0/static_local_scalar integer=writable-null pointer=const-null storage=internal-global lifetime=static zero-width=8 addressable=1'
