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
        grep -n -E 'promoted_static_proto:|read_correct:|read_hit:|read_second_counter:|branch_data_size:|read_promoted_static_arg[12]:|addi a0|li a0|slli a0|\.word' \
            "$assembly" >&2 || true
    fi
    exit 1
}

"$host_cc" -E -P -std=c11 -x c "$root/tests/compiler/c0/anonymous_record_members.c" \
    -o "$work/anonymous_record_members.i"
"$minic" -S "$work/anonymous_record_members.i" -o "$assembly"

test -s "$assembly" || fail 'assembly=missing'
for symbol in read_correct read_hit read_second_counter branch_data_size \
              read_promoted_static_arg1 read_promoted_static_arg2; do
    grep -F "$symbol:" "$assembly" >/dev/null || fail "symbol=$symbol missing"
done

grep -F '  li a0, 40' "$assembly" >/dev/null || fail 'sizeof expected=40'
offset24=$(grep -c '  addi a0, a0, 24' "$assembly" || true)
offset8=$(grep -c '  addi a0, a0, 8' "$assembly" || true)
scale8=$(grep -c '  slli a0, a0, 3' "$assembly" || true)
test "$offset24" -ge 3 || fail "anonymous-union-base expected>=3 actual=$offset24"
test "$offset8" -ge 1 || fail "promoted-second-field expected>=1 actual=$offset8"
test "$scale8" -ge 1 || fail "array-index-scale8 expected>=1 actual=$scale8"

grep -F 'promoted_static_proto:' "$assembly" >/dev/null || fail 'static-proto symbol=missing'
grep -F '  .word 7' "$assembly" >/dev/null || fail 'static-proto ret_type=7 missing'
grep -F '  .word 11' "$assembly" >/dev/null || fail 'static-proto promoted arg1=11 missing'
grep -F '  .word 22' "$assembly" >/dev/null || fail 'static-proto promoted arg2=22 missing'

printf '%s\n' 'PASS compiler/c0/anonymous_record_members union-offset=24 promoted-access=correct,hit array-overlay=index-scale8 size=40 static-promoted-designator=arg1,arg2'
