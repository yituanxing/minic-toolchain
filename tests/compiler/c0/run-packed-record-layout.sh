#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-packed-record-layout

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/packed_record_layout.c" \
    -o "$work/packed_record_layout.i"
"$minic" -S \
    "$work/packed_record_layout.i" \
    -o "$work/packed_record_layout.s"

grep -F '.size sample, 4' "$work/packed_record_layout.s" >/dev/null
grep -F '.size suffix_sample, 4' "$work/packed_record_layout.s" >/dev/null
grep -F '.size forward_sample, 3' "$work/packed_record_layout.s" >/dev/null
grep -F '  addi a0, a0, 1' "$work/packed_record_layout.s" >/dev/null
grep -F '  addi a0, a0, 3' "$work/packed_record_layout.s" >/dev/null
grep -F '  lhu a0, 0(a0)' "$work/packed_record_layout.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/packed_record_layout placement=prefix+suffix forward-definition=1 size=4 offsets=0,1,3 alignment=1'
