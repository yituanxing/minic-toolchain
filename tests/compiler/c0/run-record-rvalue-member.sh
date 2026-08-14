#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-record-rvalue-member

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/record_rvalue_member.c" -o "$work/member.i"
"$minic" -S "$work/member.i" -o "$work/member.s"
test "$(grep -c -F '  call pgprot_noncached' "$work/member.s")" -eq 1
grep -F '  sd a0, 0(sp)' "$work/member.s" >/dev/null
grep -F '  ld a0, 0(a0)' "$work/member.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/record_rvalue_member call-result=1 materialized-temp=1 scalar-member=rvalue once-only-call=1'

"$host_cc" -E -P -x c "$root/tests/compiler/c0/invalid_assign_record_rvalue_member.c" \
    -o "$work/invalid.i"
if "$minic" -S "$work/invalid.i" -o "$work/invalid.s" \
    >"$work/invalid.stdout" 2>"$work/invalid.stderr"; then
    echo 'FAIL record rvalue member assignment unexpectedly compiled' >&2
    exit 1
fi
grep -F 'assignment expression requires a modifiable object lvalue' "$work/invalid.stderr" >/dev/null
printf '%s\n' 'PASS compiler/c0/invalid_assign_record_rvalue_member nonmodifiable=1'
