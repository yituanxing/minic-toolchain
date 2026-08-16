#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-builtin-unary-family

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/builtin_unary_family.c" \
    -o "$work/builtin_unary_family.i"
"$minic" -S "$work/builtin_unary_family.i" -o "$work/builtin_unary_family.s"
test -s "$work/builtin_unary_family.s"
grep -F 'runtime_ctzl:' "$work/builtin_unary_family.s" >/dev/null
grep -F 'runtime_ffsll:' "$work/builtin_unary_family.s" >/dev/null
grep -F 'runtime_isdigit:' "$work/builtin_unary_family.s" >/dev/null
grep -F '.Lminic_ctzl_loop_' "$work/builtin_unary_family.s" >/dev/null
grep -F '.Lminic_ffsll_loop_' "$work/builtin_unary_family.s" >/dev/null
grep -F '  addi t0, a0, -48' "$work/builtin_unary_family.s" >/dev/null
grep -F '  sltiu a0, t0, 10' "$work/builtin_unary_family.s" >/dev/null

cat >"$work/ctzl-zero.c" <<'EOF'
int invalid_bound[__builtin_ctzl(0UL)];
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/ctzl-zero.c" -o "$work/ctzl-zero.i"
if "$minic" -S "$work/ctzl-zero.i" -o "$work/ctzl-zero.s" 2>"$work/ctzl-zero.stderr"; then
    printf '%s\n' '__builtin_ctzl(0) unexpectedly accepted as an integer constant expression' >&2
    exit 1
fi
grep -F 'expected integer constant expression' "$work/ctzl-zero.stderr" >/dev/null

printf '%s\n' 'PASS compiler/c0/builtin_unary_family ast=shared-unary-builtin consteval=typed ctzl=rv64i-loop ffsll=rv64i-loop isdigit=range-check zero-ctz=fail-closed'
