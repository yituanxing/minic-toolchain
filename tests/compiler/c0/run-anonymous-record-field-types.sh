#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-anonymous-record-field-types

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/anonymous_record_field_type.c" \
    -o "$work/anonymous_record_field_type.i"
"$minic" -S "$work/anonymous_record_field_type.i" \
    -o "$work/anonymous_record_field_type.s"

# Core register assignment is not part of the layout contract. Require the
# actual member offset and unsigned-halfword load/store, regardless of whether
# the lowered address/value currently lives in a0, t0, or t1.
awk '$1 == "addi" && $2 == $3 && $4 == "2" { found = 1 }
     END { exit !found }' "$work/anonymous_record_field_type.s"
grep -E '^[[:space:]]+lhu[[:space:]]+[[:alnum:]]+,[[:space:]]*0\([[:alnum:]]+\)$' \
    "$work/anonymous_record_field_type.s" >/dev/null
grep -E '^[[:space:]]+sh[[:space:]]+[[:alnum:]]+,[[:space:]]*0\([[:alnum:]]+\)$' \
    "$work/anonymous_record_field_type.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/anonymous_record_field_type nested-struct-in-union=1 layout=4 delta-offset=2'
