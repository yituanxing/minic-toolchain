#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-compound-assignment-full

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/compound_assignment_full.c" \
    -o "$work/compound_assignment_full.i"
"$minic" -S "$work/compound_assignment_full.i" \
    -o "$work/compound_assignment_full.s"

test "$(grep -c -F '  call next_slot' "$work/compound_assignment_full.s")" -eq 1
grep -F '  slli a0, a0, 2' "$work/compound_assignment_full.s" >/dev/null
grep -F '  and a0, t0, a0' "$work/compound_assignment_full.s" >/dev/null
grep -F '  or a0, t0, a0' "$work/compound_assignment_full.s" >/dev/null
grep -F '  xor a0, t0, a0' "$work/compound_assignment_full.s" >/dev/null
grep -F '  sllw a0, t0, a0' "$work/compound_assignment_full.s" >/dev/null
grep -F '  srlw a0, t0, a0' "$work/compound_assignment_full.s" >/dev/null
grep -F '  sraw a0, t0, a0' "$work/compound_assignment_full.s" >/dev/null
grep -F '  mulw a0, t0, a0' "$work/compound_assignment_full.s" >/dev/null
grep -F '  subw a0, t0, a0' "$work/compound_assignment_full.s" >/dev/null
grep -F '  divu a0, t0, a0' "$work/compound_assignment_full.s" >/dev/null
grep -F '  div a0, t0, a0' "$work/compound_assignment_full.s" >/dev/null
grep -F '  remw a0, t0, a0' "$work/compound_assignment_full.s" >/dev/null
grep -F '  remu a0, t0, a0' "$work/compound_assignment_full.s" >/dev/null
grep -F '  rem a0, t0, a0' "$work/compound_assignment_full.s" >/dev/null
grep -F '  fadd.d ft0, ft0, ft1' "$work/compound_assignment_full.s" >/dev/null
grep -F '  fsub.d ft0, ft0, ft1' "$work/compound_assignment_full.s" >/dev/null
grep -F '  fmul.d ft0, ft0, ft1' "$work/compound_assignment_full.s" >/dev/null
grep -F '  fdiv.d ft0, ft0, ft1' "$work/compound_assignment_full.s" >/dev/null
grep -F '  fcvt.d.w ft1, a0' "$work/compound_assignment_full.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/compound_assignment_full integer=+=,-=,*=,/=,%=,&=,|=,^=,<<=,>>= shift-right=signed+unsigned pointer=+,- double=+=,-=,*=,/= mixed-int-rhs=1 lvalue-evaluation=once'
