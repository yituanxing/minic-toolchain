#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-builtin-clzll

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/builtin_clzll.c" -o "$work/builtin_clzll.i"
"$minic" -S "$work/builtin_clzll.i" -o "$work/builtin_clzll.s"
test -s "$work/builtin_clzll.s"
grep -F 'runtime_clzll_ull:' "$work/builtin_clzll.s" >/dev/null
grep -F 'runtime_clzll_uint:' "$work/builtin_clzll.s" >/dev/null
grep -F '.Lminic_clzll_32_' "$work/builtin_clzll.s" >/dev/null
grep -F '  srli t1, a0, 32' "$work/builtin_clzll.s" >/dev/null
if grep -Eq '(^|[[:space:]])clz([[:space:]]|$)' "$work/builtin_clzll.s"; then
    printf '%s
' 'unexpected Zbb clz dependency' >&2
    exit 1
fi
printf '%s
' 'PASS compiler/c0/builtin_clzll ast=unary-builtin argument=ull-normalized runtime-lowering=rv64i-binary-search consteval=deferred zbb=none'
