#!/usr/bin/env sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-$root/build/debug/bin/minic}
host_cc=${HOST_CC:-cc}
work=${BUILD_DIR:-$root/build/external-tentative-definitions}
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/external_tentative_definitions.c" \
    -o "$work/external_tentative_definitions.i"
"$minic" -S "$work/external_tentative_definitions.i" \
    -o "$work/external_tentative_definitions.s"
assembly="$work/external_tentative_definitions.s"

grep -F 'early_boot_irqs_disabled:' "$assembly" >/dev/null
grep -F 'system_state:' "$assembly" >/dev/null
grep -F 'late_time_init:' "$assembly" >/dev/null
grep -F 'boot_command_line:' "$assembly" >/dev/null
grep -F 'saved_command_line:' "$assembly" >/dev/null
grep -F '.section .init.data' "$assembly" >/dev/null
grep -F '.section .data..ro_after_init' "$assembly" >/dev/null
grep -F '.zero 1024' "$assembly" >/dev/null
grep -F '.word 7' "$assembly" >/dev/null
grep -F '.word 9' "$assembly" >/dev/null

test "$(grep -c '^repeated_tentative:' "$assembly")" -eq 1
test "$(grep -c '^extern_then_tentative:' "$assembly")" -eq 1
test "$(grep -c '^tentative_then_extern:' "$assembly")" -eq 1
test "$(grep -c '^tentative_then_full:' "$assembly")" -eq 1
test "$(grep -c '^full_then_tentative:' "$assembly")" -eq 1

if "$minic" -S "$root/tests/compiler/c0/invalid_external_tentative_incomplete_array.c" \
    -o "$work/invalid-incomplete.s" 2>"$work/invalid-incomplete.stderr"; then
    printf '%s\n' 'incomplete tentative array unexpectedly accepted' >&2
    exit 1
fi
grep -F 'incomplete external tentative array is not implemented yet' \
    "$work/invalid-incomplete.stderr" >/dev/null

if "$minic" -S "$root/tests/compiler/c0/invalid_external_tentative_redeclaration.c" \
    -o "$work/invalid-redecl.s" 2>"$work/invalid-redecl.stderr"; then
    printf '%s\n' 'conflicting tentative redeclaration unexpectedly accepted' >&2
    exit 1
fi
grep -F 'conflicting external tentative definition' "$work/invalid-redecl.stderr" >/dev/null

printf '%s\n' 'PASS compiler/c0/external_tentative_definitions state=extern|tentative|defined zero=end-of-tu fixed-array=1 attrs=section suffix=1 incomplete-array=fail-closed'
