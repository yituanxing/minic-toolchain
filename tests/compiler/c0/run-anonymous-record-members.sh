#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-anonymous-record-members
assembly="$work/anonymous_record_members.s"

rm -rf "$work"
mkdir -p "$work"

fail() {
    printf '%s\n' "FAIL compiler/c0/anonymous_record_members $*" >&2
    if test -s "$assembly"; then
        printf '%s\n' 'anonymous member assembly evidence:' >&2
        grep -n -E 'read_correct:|read_hit:|read_second_counter:|branch_data_size:|addi a0|li a0' \
            "$assembly" >&2 || true
    fi
    exit 1
}

"$host_cc" -E -P -std=c11 -x c "$root/tests/compiler/c0/anonymous_record_members.c" \
    -o "$work/anonymous_record_members.i"
"$minic" -S "$work/anonymous_record_members.i" -o "$assembly"

test -s "$assembly" || fail 'assembly=missing'
for symbol in read_correct read_hit read_second_counter branch_data_size; do
    grep -F "$symbol:" "$assembly" >/dev/null || fail "symbol=$symbol missing"
done

grep -F '  li a0, 40' "$assembly" >/dev/null || fail 'sizeof expected=40'
offset24=$(grep -c '  addi a0, a0, 24' "$assembly" || true)
offset8=$(grep -c '  addi a0, a0, 8' "$assembly" || true)
test "$offset24" -ge 1 || fail "offset24 expected>=1 actual=$offset24"
test "$offset8" -ge 2 || fail "offset8 expected>=2 actual=$offset8"

printf '%s\n' 'PASS compiler/c0/anonymous_record_members union=1 anonymous-structs=2 promoted-access=correct,hit array-overlay=1 size=40'
