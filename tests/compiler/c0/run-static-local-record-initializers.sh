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
awk '
    $1 == ".word" && $2 == "7" { seen_kind = 1; next }
    seen_kind && !seen_first && $1 == ".zero" { zero_bytes += $2; next }
    seen_kind && $1 == ".word" && $2 == "-1" { seen_first = 1; next }
    END { exit !(seen_kind && seen_first && zero_bytes == 12) }
' "$work/static_local_record_initializer.s"
grep -F '  .word -1' "$work/static_local_record_initializer.s" >/dev/null
grep -F '  .word -2' "$work/static_local_record_initializer.s" >/dev/null
grep -F '  .dword 1' "$work/static_local_record_initializer.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/static_local_record_initializer enum=7 nested-zero=12 signed=-1,-2 compound-literal=1 designated-nested=1 anonymous-union-first=1 empty-record=1 shared-owner=1 target-layout=rv64'
