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

# A one-slot aggregate returns in a0 by RV64 ABI, but Core owns the private
# result object's stack offset. Require that the call result is materialized
# after the call, then require a scalar member load through an address value.
awk '
  /call pgprot_noncached/ { after_call=1; next }
  after_call && /^[[:space:]]+sd[[:space:]]+a0,[[:space:]]*-?[0-9]+\(sp\)$/ {
      materialized=1
  }
  materialized &&
      /^[[:space:]]+ld[[:space:]]+[a-z][a-z0-9]*,[[:space:]]*0\([a-z][a-z0-9]*\)$/ {
      loaded=1
  }
  END { exit (after_call && materialized && loaded) ? 0 : 1 }
' "$work/member.s"
printf '%s\n' 'PASS compiler/c0/record_rvalue_member call-result=1 materialized-temp=1 scalar-member=rvalue once-only-call=1 normalized=core-object'

"$host_cc" -E -P -x c "$root/tests/compiler/c0/invalid_assign_record_rvalue_member.c" \
    -o "$work/invalid.i"
if "$minic" -S "$work/invalid.i" -o "$work/invalid.s" \
    >"$work/invalid.stdout" 2>"$work/invalid.stderr"; then
    echo 'FAIL record rvalue member assignment unexpectedly compiled' >&2
    exit 1
fi
grep -F 'assignment expression requires a modifiable object lvalue' "$work/invalid.stderr" >/dev/null
printf '%s\n' 'PASS compiler/c0/invalid_assign_record_rvalue_member nonmodifiable=1'
