#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-rv64-integer-aggregate-return
assembly="$work/rv64_integer_aggregate_return.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/rv64_integer_aggregate_return.c" \
    -o "$work/rv64_integer_aggregate_return.i"
"$minic" -S "$work/rv64_integer_aggregate_return.i" -o "$assembly"

test -s "$assembly"
grep -F 'add_pair:' "$assembly" >/dev/null
grep -F 'forward_pair:' "$assembly" >/dev/null
grep -F 'call add_pair' "$assembly" >/dev/null
grep -F 'sd a0,' "$assembly" >/dev/null
grep -F 'sd a1,' "$assembly" >/dev/null
grep -F 'sd a2,' "$assembly" >/dev/null
grep -F 'sd a3,' "$assembly" >/dev/null
grep -F 'ld a0, 0(t0)' "$assembly" >/dev/null
grep -F 'ld a1, 8(t0)' "$assembly" >/dev/null
grep -F 'make_triple:' "$assembly" >/dev/null
grep -F 'forward_triple:' "$assembly" >/dev/null
grep -F 'cleanup_triple_call:' "$assembly" >/dev/null
grep -F 'call make_triple' "$assembly" >/dev/null
grep -F 'lbu t0, 0(t2)' "$assembly" >/dev/null
grep -F 'sb t0, 0(t3)' "$assembly" >/dev/null

printf '%s\n' 'PASS compiler/c0/rv64_integer_aggregate_return direct=8,16 indirect=24 hidden-result=a0 explicit-args=a1+ record-local=1 record-call=1 cleanup=1'
