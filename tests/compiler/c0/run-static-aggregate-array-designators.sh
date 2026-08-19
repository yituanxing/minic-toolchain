#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-static-aggregate-array-designators

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/static_aggregate_array_designators.c" \
    -o "$work/static_aggregate_array_designators.i"
"$minic" -S "$work/static_aggregate_array_designators.i" \
    -o "$work/static_aggregate_array_designators.s"

grep -F '.size sparse_records, 32' "$work/static_aggregate_array_designators.s" >/dev/null
grep -F '.size nested_records, 32' "$work/static_aggregate_array_designators.s" >/dev/null
grep -F '.size fixed_records, 32' "$work/static_aggregate_array_designators.s" >/dev/null
grep -F '  .word 11' "$work/static_aggregate_array_designators.s" >/dev/null
grep -F '  .word 32' "$work/static_aggregate_array_designators.s" >/dev/null
grep -F '  .word 52' "$work/static_aggregate_array_designators.s" >/dev/null

source="$root/tests/compiler/c0/invalid_static_aggregate_array_backward_designator.c"
"$host_cc" -E -P -std=gnu11 -x c "$source" -o "$work/backward.i"
if "$minic" -S "$work/backward.i" -o "$work/backward.s" \
    >"$work/backward.stdout" 2>"$work/backward.stderr"; then
    printf '%s\n' 'FAIL backward static aggregate array designator unexpectedly succeeded' >&2
    exit 1
fi
grep -F 'backward static aggregate array designator is not supported yet' \
    "$work/backward.stderr" >/dev/null

printf '%s\n' \
    'PASS compiler/c0/static_aggregate_array_designators inferred-bound=designator-extent nested-field=1 compound-literal=1 backward=fail-closed range=shared-owner'
