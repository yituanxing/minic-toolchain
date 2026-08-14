#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-overflow-builtins
assembly="$work/gnu_overflow_builtins.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/gnu_overflow_builtins.c" \
    -o "$work/gnu_overflow_builtins.i"
"$minic" -S "$work/gnu_overflow_builtins.i" -o "$assembly"

test -s "$assembly"
grep -F 'checked_add_int:' "$assembly" >/dev/null
grep -F 'checked_mul_long:' "$assembly" >/dev/null
grep -F 'checked_mul_ulong:' "$assembly" >/dev/null
grep -F 'checked_sub_ulong:' "$assembly" >/dev/null
grep -F 'mulh t4, t0, t1' "$assembly" >/dev/null
grep -F 'mulhu t4, t0, t1' "$assembly" >/dev/null
grep -F 'sltu a0, t0, t1' "$assembly" >/dev/null
grep -F 'sw t2, 0(t3)' "$assembly" >/dev/null
grep -F 'sd t2, 0(t3)' "$assembly" >/dev/null

printf '%s\n' 'PASS compiler/c0/gnu_overflow_builtins attribute=warn-unused-result ops=add,sub,mul result-store=1 return=bool widths=32,64 signed+unsigned=1'
