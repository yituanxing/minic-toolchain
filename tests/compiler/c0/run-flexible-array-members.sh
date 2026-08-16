#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-flexible-array-members

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/flexible_array_member.c" \
    -o "$work/flexible_array_member.i"
"$minic" -S \
    "$work/flexible_array_member.i" \
    -o "$work/flexible_array_member.s"
grep -F '  addi a0, a0, 3' "$work/flexible_array_member.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/flexible_array_member packed-size=3 payload-offset=3'


"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/static_nested_flexible_array_initializer.c" \
    -o "$work/static_nested_flexible_array_initializer.i"
"$minic" -S \
    "$work/static_nested_flexible_array_initializer.i" \
    -o "$work/static_nested_flexible_array_initializer.s"
grep -F '.size nested_fam_rows, 64' \
    "$work/static_nested_flexible_array_initializer.s" >/dev/null
grep -F '.dword .Lminic_string_' \
    "$work/static_nested_flexible_array_initializer.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/static_nested_flexible_array_initializer linux-wrapper zero-slot-fam + relocation'

expect_failure() {
    name=$1
    expected=$2
    "$host_cc" -E -P -x c "$root/tests/compiler/c0/$name.c" -o "$work/$name.i"
    if "$minic" -S "$work/$name.i" -o "$work/$name.s" \
        >"$work/$name.stdout" 2>"$work/$name.stderr"; then
        printf '%s\n' "FAIL compiler/c0/$name: compilation unexpectedly succeeded" >&2
        exit 1
    fi
    grep -F "$expected" "$work/$name.stderr" >/dev/null
    printf '%s\n' "PASS compiler/c0/$name"
}

expect_failure invalid_flexible_array_not_last \
    'flexible array member must be the last record field'
expect_failure invalid_union_flexible_array \
    'flexible array member is not allowed in union'
