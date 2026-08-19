#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/first500-static-array-pointer-v1
mkdir -p "$work"

for name in static_local_pointer_array_decay inferred_static_unsigned_char_list; do
  "$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/$name.c" -o "$work/$name.i"
  "$minic" -S "$work/$name.i" -o "$work/$name.s"
  test -s "$work/$name.s"
done

printf '%s\n' 'PASS compiler/c0/first500-static-array-pointer-v1 local-pointer-relocation=1 inferred-uchar-list=1'
