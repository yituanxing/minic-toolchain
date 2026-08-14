#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-external-scalar-definitions

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/external_scalar_definition.c" \
    -o "$work/external_scalar_definition.i"
"$minic" -S "$work/external_scalar_definition.i" \
    -o "$work/external_scalar_definition.s"

grep -F '.globl external_count' "$work/external_scalar_definition.s" >/dev/null
grep -F 'external_count:' "$work/external_scalar_definition.s" >/dev/null
grep -F '  .word 7' "$work/external_scalar_definition.s" >/dev/null
grep -F '.globl external_wide' "$work/external_scalar_definition.s" >/dev/null
grep -F '  .dword 11' "$work/external_scalar_definition.s" >/dev/null
grep -F '.globl loops_per_jiffy' "$work/external_scalar_definition.s" >/dev/null
grep -F 'loops_per_jiffy:' "$work/external_scalar_definition.s" >/dev/null
grep -F '  .dword 4096' "$work/external_scalar_definition.s" >/dev/null
grep -F 'internal_folded:' "$work/external_scalar_definition.s" >/dev/null
grep -F '  .word 16' "$work/external_scalar_definition.s" >/dev/null
if grep -F '.globl internal_folded' "$work/external_scalar_definition.s" >/dev/null; then
    printf '%s\n' 'FAIL compiler/c0/external_scalar_definition: internal scalar exported' >&2
    exit 1
fi
test "$(grep -c '^external_count:$' "$work/external_scalar_definition.s")" -eq 1

expect_failure() {
    name=$1
    message=$2

    if "$minic" -S "$root/tests/compiler/c0/$name.c" -o "$work/$name.s" \
        >"$work/$name.stdout" 2>"$work/$name.stderr"; then
        printf '%s\n' "FAIL compiler/c0/$name: compilation unexpectedly succeeded" >&2
        exit 1
    fi
    if ! grep -F "$message" "$work/$name.stderr" >/dev/null; then
        printf '%s\n' "FAIL compiler/c0/$name: diagnostic mismatch" >&2
        cat "$work/$name.stderr" >&2
        exit 1
    fi
    printf '%s\n' "PASS compiler/c0/$name"
}

expect_failure invalid_external_integer_nonconstant \
    'integer initializer requires an integer constant expression'
expect_failure invalid_external_integer_payload_range \
    'integer initializer exceeds current global payload range'

printf '%s\n' \
    'PASS compiler/c0/external_scalar_definition extern-merge=1 typed-consteval=1 int=.word long=.dword static=shared payload=int-bounded'
