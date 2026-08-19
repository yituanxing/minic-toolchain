#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/first500-pareto-v1
mkdir -p "$work"

for name in identity_record_typedef_cast record_field_nonstring_attribute; do
  "$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/$name.c" -o "$work/$name.i"
  "$minic" -S "$work/$name.i" -o "$work/$name.s"
  test -s "$work/$name.s"
done

printf '%s\n' 'PASS compiler/c0/first500-pareto-v1 identity-record-cast=1 nonstring-field=1'
