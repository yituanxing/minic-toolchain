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
grep -F 'ld a0, 0(t5)' "$assembly" >/dev/null
grep -F 'ld a1, 0(t5)' "$assembly" >/dev/null

grep -F 'unwrap_word:' "$assembly" >/dev/null
grep -F 'return_word:' "$assembly" >/dev/null
grep -F 'call unwrap_word' "$assembly" >/dev/null
grep -F '  sw a0,' "$assembly" >/dev/null
grep -F '  lwu t1, 0(t5)' "$assembly" >/dev/null

printf '%s\n' 'PASS compiler/c0/rv64_integer_aggregate_return sizes=4,16 class=integer callee-partial=exact caller-partial=exact return-partial=exact record-call=1'
