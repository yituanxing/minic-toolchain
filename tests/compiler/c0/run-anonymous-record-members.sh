#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-anonymous-record-members

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=c11 -x c "$root/tests/compiler/c0/anonymous_record_members.c" \
    -o "$work/anonymous_record_members.i"
"$minic" -S "$work/anonymous_record_members.i" \
    -o "$work/anonymous_record_members.s"

test -s "$work/anonymous_record_members.s"
grep -F 'read_correct:' "$work/anonymous_record_members.s" >/dev/null
grep -F 'read_hit:' "$work/anonymous_record_members.s" >/dev/null
grep -F 'read_second_counter:' "$work/anonymous_record_members.s" >/dev/null
grep -F 'branch_data_size:' "$work/anonymous_record_members.s" >/dev/null
grep -F '  li a0, 40' "$work/anonymous_record_members.s" >/dev/null
test "$(grep -c '  addi a0, a0, 24' "$work/anonymous_record_members.s")" -ge 1
test "$(grep -c '  addi a0, a0, 8' "$work/anonymous_record_members.s")" -ge 2

printf '%s\n' 'PASS compiler/c0/anonymous_record_members union=1 anonymous-structs=2 promoted-access=correct,hit array-overlay=1 size=40'
