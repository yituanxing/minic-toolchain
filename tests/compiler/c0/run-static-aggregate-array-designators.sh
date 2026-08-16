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

for case_name in backward range; do
    source="$root/tests/compiler/c0/invalid_static_aggregate_array_${case_name}_designator.c"
    "$host_cc" -E -P -std=gnu11 -x c "$source" -o "$work/$case_name.i"
    if "$minic" -S "$work/$case_name.i" -o "$work/$case_name.s" \
        >"$work/$case_name.stdout" 2>"$work/$case_name.stderr"; then
        printf '%s\n' "FAIL static aggregate array $case_name designator unexpectedly succeeded" >&2
        exit 1
    fi
done

grep -F 'backward static aggregate array designator is not supported yet' \
    "$work/backward.stderr" >/dev/null
grep -F 'GNU range designators for aggregate static arrays are not supported yet' \
    "$work/range.stderr" >/dev/null

printf '%s\n' \
    'PASS compiler/c0/static_aggregate_array_designators inferred-bound=designator-extent nested-field=1 compound-literal=1 backward=fail-closed range=fail-closed'
