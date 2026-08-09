#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-record-copy-array-members

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/record_copy_array_member.c" \
    -o "$work/record_copy_array_member.i"
"$minic" -S \
    "$work/record_copy_array_member.i" \
    -o "$work/record_copy_array_member.s"

grep -F '.type copy_packet, @function' "$work/record_copy_array_member.s" >/dev/null
grep -F '  lbu t0, 0(t2)' "$work/record_copy_array_member.s" >/dev/null
grep -F '  sb t0, 0(t3)' "$work/record_copy_array_member.s" >/dev/null
# Packet is 12 bytes on RV64 here; the snapshot is rounded to a 16-byte temporary stack slot.
test "$(grep -c -F '  lbu t0, 0(t2)' "$work/record_copy_array_member.s")" -ge 24
grep -F '  addi sp, sp, -16' "$work/record_copy_array_member.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/record_copy_array_member storage=12 snapshot=stack array-member=yes'
