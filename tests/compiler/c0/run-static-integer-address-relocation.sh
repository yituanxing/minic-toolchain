#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/static-integer-address-relocation
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c \
  "$root/tests/compiler/c0/static_integer_address_relocation.c" \
  -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
test -s "$work/output.s"
grep -q 'init_stack' "$work/output.s"
