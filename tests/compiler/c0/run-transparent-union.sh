#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-transparent-union

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/transparent_union.c" -o "$work/transparent.i"
"$minic" -S "$work/transparent.i" -o "$work/transparent.s"
grep -F '  call release_pages' "$work/transparent.s" >/dev/null
grep -F '  jalr ra, t0, 0' "$work/transparent.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/transparent_union type-owner=record members=3 direct=3 indirect=1 null=1 abi=first-member-pointer callee=one-integer-chunk'

"$host_cc" -E -P -x c "$root/tests/compiler/c0/invalid_transparent_union_nonmember.c" -o "$work/nonmember.i"
if "$minic" -S "$work/nonmember.i" -o "$work/nonmember.s" >"$work/nonmember.stdout" 2>"$work/nonmember.stderr"; then
    echo 'FAIL transparent union non-member argument unexpectedly compiled' >&2
    exit 1
fi
grep -F 'call argument type does not match declaration' "$work/nonmember.stderr" >/dev/null

"$host_cc" -E -P -x c "$root/tests/compiler/c0/invalid_transparent_union_nonunion.c" -o "$work/nonunion.i"
if "$minic" -S "$work/nonunion.i" -o "$work/nonunion.s" >"$work/nonunion.stdout" 2>"$work/nonunion.stderr"; then
    echo 'FAIL transparent_union on non-union unexpectedly compiled' >&2
    exit 1
fi
grep -F 'GNU transparent_union requires a union type' "$work/nonunion.stderr" >/dev/null

"$host_cc" -E -P -x c "$root/tests/compiler/c0/unsupported_transparent_union_nonpointer.c" -o "$work/nonpointer.i"
if "$minic" -S "$work/nonpointer.i" -o "$work/nonpointer.s" >"$work/nonpointer.stdout" 2>"$work/nonpointer.stderr"; then
    echo 'FAIL unsupported mixed transparent union unexpectedly compiled' >&2
    exit 1
fi
grep -F 'GNU transparent_union v0 requires pointer members with one machine representation' "$work/nonpointer.stderr" >/dev/null
printf '%s\n' 'PASS compiler/c0/transparent_union negative=nonmember+nonunion+nonpointer-v0'
