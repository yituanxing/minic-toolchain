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
# Runtime ctzl and ffsll are both Core-owned. ffsll is expressed through
# the same target-neutral CTZ primitive, so require per-function Core CTZ loops
# rather than legacy builtin-specific target labels.
grep -F '.Lruntime_ctzl_core_ctz_loop_' "$work/builtin_unary_family.s" >/dev/null
grep -F '.Lruntime_ffsll_core_ctz_loop_' "$work/builtin_unary_family.s" >/dev/null

# isdigit is normalized in Core as unsigned(value) - 48 < 10. Preserve that
# semantic range contract without pinning legacy register allocation or the
# addi/sltiu peephole.
awk '
  /runtime_isdigit:/ { in_fn=1; c48=0; c10=0; sub=0; less=0 }
  in_fn && /^[[:space:]]+li[[:space:]]+[^,]+,[[:space:]]*48$/ { c48=1 }
  in_fn && /^[[:space:]]+li[[:space:]]+[^,]+,[[:space:]]*10$/ { c10=1 }
  in_fn && /^[[:space:]]+sub[[:space:]]+/ { sub=1 }
  in_fn && /^[[:space:]]+sltu[[:space:]]+/ { less=1 }
  in_fn && /^\.size[[:space:]]+runtime_isdigit/ { exit (c48 && c10 && sub && less) ? 0 : 1 }
  END { if (!in_fn) exit 1 }
' "$work/builtin_unary_family.s"

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
