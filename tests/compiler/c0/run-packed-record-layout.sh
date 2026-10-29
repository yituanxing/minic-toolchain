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
# Core now materializes the field address in a temporary before writing the
# return register. Assert the layout offsets and machine access width without
# coupling this regression to the allocator's choice of a0 versus t0/t1.
grep -E '^[[:space:]]+addi [[:alnum:]]+, [[:alnum:]]+, 1$' "$work/packed_record_layout.s" >/dev/null
grep -E '^[[:space:]]+addi [[:alnum:]]+, [[:alnum:]]+, 3$' "$work/packed_record_layout.s" >/dev/null
grep -E '^[[:space:]]+lhu [[:alnum:]]+, 0\([[:alnum:]]+\)$' "$work/packed_record_layout.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/packed_record_layout placement=prefix+suffix forward-definition=1 size=4 offsets=0,1,3 alignment=1'
