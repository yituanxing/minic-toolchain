#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-restrict-qualifiers

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/restrict_qualifiers.c" \
    -o "$work/restrict_qualifiers.i"
"$minic" -S "$work/restrict_qualifiers.i" \
    -o "$work/restrict_qualifiers.s"

test -s "$work/restrict_qualifiers.s"
grep -F 'call_restrict_forms:' "$work/restrict_qualifiers.s" >/dev/null
grep -F '  call copy_gnu' "$work/restrict_qualifiers.s" >/dev/null
grep -F '  call copy_gnu_double' "$work/restrict_qualifiers.s" >/dev/null
grep -F '  call copy_c' "$work/restrict_qualifiers.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/restrict_qualifiers C=restrict GNU=__restrict/__restrict__ pointer-only=1 abi=unchanged'
