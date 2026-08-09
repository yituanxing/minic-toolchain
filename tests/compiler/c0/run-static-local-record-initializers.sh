#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-local-record-initializer

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/static_local_record_initializer.c" \
    -o "$work/static_local_record_initializer.i"
"$minic" -S "$work/static_local_record_initializer.i" \
    -o "$work/static_local_record_initializer.s"

grep -F '.type __minic_static_local_' "$work/static_local_record_initializer.s" >/dev/null
grep -F '  .word 7' "$work/static_local_record_initializer.s" >/dev/null
grep -F '  .zero 12' "$work/static_local_record_initializer.s" >/dev/null
grep -F '  .word -1' "$work/static_local_record_initializer.s" >/dev/null
grep -F '  .word -2' "$work/static_local_record_initializer.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/static_local_record_initializer enum=7 nested-zero=1 signed=-1,-2 target-layout=rv64'
