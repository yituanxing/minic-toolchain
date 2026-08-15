#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-external-record-compound-literal

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/external_record_compound_literal.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
test -s "$work/output.s"
grep -F '.globl sched_numa_balancing' "$work/output.s" >/dev/null
grep -F 'sched_numa_balancing:' "$work/output.s" >/dev/null
grep -F 'main:' "$work/output.s" >/dev/null
first_line=$(grep -n -F '  .dword earlier_target' "$work/output.s" | head -n 1 | cut -d: -f1)
marker_line=$(grep -n -F '  .word 7' "$work/output.s" | head -n 1 | cut -d: -f1)
last_line=$(grep -n -F '  .dword later_target' "$work/output.s" | head -n 1 | cut -d: -f1)
test -n "$first_line"
test -n "$marker_line"
test -n "$last_line"
test "$first_line" -lt "$marker_line"
test "$marker_line" -lt "$last_line"

cat >"$work/nonfirst-union.c" <<'EOF'
union payload { unsigned long first; void *second; };
struct wrapper { union payload value; };
struct wrapper rejected = (struct wrapper){ .value = { .second = (void *)0 } };
EOF
"$host_cc" -E -P -std=gnu11 -x c "$work/nonfirst-union.c" -o "$work/nonfirst-union.i"
if "$minic" -S "$work/nonfirst-union.i" -o "$work/nonfirst-union.s" \
    >"$work/nonfirst-union.out" 2>"$work/nonfirst-union.err"; then
    printf '%s\n' 'FAIL compiler/c0/external-record-compound-literal: non-first union designator exceeded v0 representation' >&2
    exit 1
fi
grep -F 'nested static union designator requires the representable first member' \
    "$work/nonfirst-union.err" >/dev/null

printf '%s\n' 'PASS compiler/c0/external-record-compound-literal linkage=external storage=static-duration initializer=shared-constant-owner compound-literal=record anonymous-union-first-designator=1 backward-direct-scalar-designators=1 relocation-order=canonical nonfirst=fail-closed'
