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

printf '%s\n' 'PASS compiler/c0/rv64_integer_aggregate_return size=16 class=integer callee-params=a0-a3 caller-chunks=1 return=a0-a1 record-local=1 record-call=1'
