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
grep -F '.type __minic_static_local_' "$work/static_local_scalar.s" >/dev/null
grep -F '  .zero 8' "$work/static_local_scalar.s" >/dev/null
if grep -F '.globl __minic_static_local_' "$work/static_local_scalar.s" >/dev/null; then
    echo 'static local scalar leaked external linkage' >&2
    exit 1
fi
test "$(grep -c -F '__minic_static_local_' "$work/static_local_scalar.s")" -ge 3
printf '%s\n' 'PASS compiler/c0/static_local_scalar writable=1 storage=internal-global lifetime=static zero-width=8 addressable=compile-verified'
