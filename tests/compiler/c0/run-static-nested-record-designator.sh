#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
build_dir=${BUILD_DIR:-"$root/build/debug"}
work="$build_dir/tests/compiler-c0-static-nested-record-designator"
mkdir -p "$work"

"$minic" -S \
    "$root/tests/programs/c0/static_nested_record_designator.c" \
    -o "$work/positive.s"
for value in 3 7 9; do
    grep -F "  .word $value" "$work/positive.s" >/dev/null
done
grep -F '  .word 0' "$work/positive.s" >/dev/null

if "$minic" -S \
    "$root/tests/compiler/c0/invalid_static_nested_record_designator_backward.c" \
    -o "$work/backward.s" >"$work/backward.stdout" 2>"$work/backward.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/static-nested-record-designator-backward: unexpectedly succeeded' >&2
    exit 1
fi
grep -F 'static record designator cannot move backward in v0' "$work/backward.stderr" >/dev/null

printf '%s\n' \
    'PASS compiler/c0/static-nested-record-designator direct-member=1 skipped-fields=zero continuation=next-field backward=fail-closed'
