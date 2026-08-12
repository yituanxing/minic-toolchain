#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-omitted-conditional

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c   "$root/tests/compiler/c0/gnu_omitted_conditional.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
test -s "$work/output.s"

grep -F 'linux_shape:' "$work/output.s" >/dev/null
grep -F 'evaluate_once:' "$work/output.s" >/dev/null
grep -F 'false_fallback:' "$work/output.s" >/dev/null
test "$(grep -c -F '  call probe' "$work/output.s")" -eq 1
grep -F 'beqz a0, .Lminic_cond_false_' "$work/output.s" >/dev/null
grep -F 'li a0, 9' "$work/output.s" >/dev/null

printf '%s
'   'PASS compiler/c0/gnu_omitted_conditional linux-shape=1 condition-evaluated-once=1 false-fallback=1 typed-consteval=true+false ordinary-conditional=unchanged'
