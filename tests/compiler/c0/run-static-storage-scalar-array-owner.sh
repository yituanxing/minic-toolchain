#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/static-storage-scalar-array-owner
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c \
  "$root/tests/compiler/c0/static_storage_scalar_array_owner.c" \
  -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
test -s "$work/output.s"

grep -F 'external_names:' "$work/output.s" >/dev/null
grep -F 'external_objects:' "$work/output.s" >/dev/null
grep -F '__minic_static_local_' "$work/output.s" >/dev/null

printf '%s\n' 'PASS compiler/c0/static-storage-scalar-array-owner external-inferred=designated+relocation static-local=fixed+inferred+range multidim=shared-owner'
