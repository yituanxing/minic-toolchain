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
    printf '%s\n' 'unexpected Zbb clz dependency' >&2
    exit 1
fi

cat >"$work/clzll-zero.c" <<'EOF'
int invalid_bound[__builtin_clzll(0ULL)];
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/clzll-zero.c" -o "$work/clzll-zero.i"
if "$minic" -S "$work/clzll-zero.i" -o "$work/clzll-zero.s" 2>"$work/clzll-zero.stderr"; then
    printf '%s\n' '__builtin_clzll(0) unexpectedly accepted as an integer constant expression' >&2
    exit 1
fi
grep -F 'expected integer constant expression' "$work/clzll-zero.stderr" >/dev/null

printf '%s\n' 'PASS compiler/c0/builtin_clzll ast=unary-builtin argument=ull-normalized runtime-lowering=rv64i-binary-search typed-consteval=nonzero linux-array-bound=1 zero=fail-closed zbb=none'
