#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
minic_cc=${MINIC_CC:-"$root/build/debug/bin/minic-cc"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-anonymous-record-members
assembly="$work/anonymous_record_members.s"

rm -rf "$work"
mkdir -p "$work"

fail() {
    printf '%s\n' "FAIL compiler/c0/anonymous_record_members $*" >&2
    if test -s "$assembly"; then
        printf '%s\n' 'anonymous member assembly evidence:' >&2
        grep -n -E 'read_correct:|read_hit:|read_second_counter:|branch_data_size:' \
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

"$minic_cc" --target=x86_64-linux -S "$work/anonymous_record_members.i" \
    -o "$work/native.s" || fail 'native assembly emission'
"$host_cc" -no-pie "$work/native.s" \
    "$root/tests/compiler/c0/anonymous_record_members_runtime.c" \
    -o "$work/native" || fail 'native runtime link'
"$work/native" || fail 'native anonymous-member address/overlay/size regression'

printf '%s\n' 'PASS compiler/c0/anonymous_record_members GNU-anonymous-union=1 promoted-access=correct,hit array-overlay=index-scale8 size=40 x86-native=1'
