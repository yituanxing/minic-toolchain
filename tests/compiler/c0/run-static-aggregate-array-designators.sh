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
grep -F '.size relocation_records, 16' "$work/static_aggregate_array_designators.s" >/dev/null
grep -F '  .dword relocation_target' "$work/static_aggregate_array_designators.s" >/dev/null
grep -F '  .dword .Lminic_string_' "$work/static_aggregate_array_designators.s" >/dev/null
grep -F '.size atomic_lock_slots, 1024' "$work/static_aggregate_array_designators.s" >/dev/null
grep -F '.size task_mask, 32' "$work/static_aggregate_array_designators.s" >/dev/null

minic_cc=$(dirname "$minic")/minic-cc
"$minic_cc" --target=x86_64-linux -S "$work/static_aggregate_array_designators.i" \
    -o "$work/static_aggregate_array_designators.x86.s"
"$host_cc" -no-pie "$work/static_aggregate_array_designators.x86.s" \
    -o "$work/static_aggregate_array_designators.x86"
"$work/static_aggregate_array_designators.x86"

source="$root/tests/compiler/c0/invalid_static_aggregate_array_backward_designator.c"
"$host_cc" -E -P -std=gnu11 -x c "$source" -o "$work/backward.i"
"$minic" -S "$work/backward.i" -o "$work/backward.s" \
    >"$work/backward.stdout" 2>"$work/backward.stderr"
# GNU designated initializers allow moving backward to a previously untouched
# slot; the emitted object must keep the original slot indices intact.
grep -F '.size bad, 24' "$work/backward.s" >/dev/null
grep -F '  .word 3' "$work/backward.s" >/dev/null
grep -F '  .word 4' "$work/backward.s" >/dev/null
grep -F '  .word 1' "$work/backward.s" >/dev/null
grep -F '  .word 2' "$work/backward.s" >/dev/null

printf '%s\n' \
    'PASS compiler/c0/static_aggregate_array_designators inferred-bound=designator-extent nested-field=1 compound-literal=1 relocation-owner=aggregate-scalar union-array=1024 backward=GNU-slot-order range=shared-owner backward-scalar-range=native-x86'
