#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-pointer-members

mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/programs/c0/pointer_member.c" \
    -o "$work/pointer_member.i"
"$minic" -S "$work/pointer_member.i" -o "$work/pointer_member.s"
grep -F "  addi a0, a0, 4" "$work/pointer_member.s" >/dev/null
grep -F "  addi a0, a0, 20" "$work/pointer_member.s" >/dev/null
grep -F "  call sum_four" "$work/pointer_member.s" >/dev/null
printf '%s\n' "PASS compiler/c0/pointer_member"

"$host_cc" -E -P -x c \
    "$root/tests/programs/c0/direct_record_member.c" \
    -o "$work/direct_record_member.i"
"$minic" -S \
    "$work/direct_record_member.i" \
    -o "$work/direct_record_member.s"
grep -F "  addi a0, a0, 4" "$work/direct_record_member.s" >/dev/null
grep -F "  la a0, global_pair" "$work/direct_record_member.s" >/dev/null
printf '%s\n' "PASS compiler/c0/direct_record_member"

"$host_cc" -E -P -x c \
    "$root/tests/programs/c0/self_referential_record.c" \
    -o "$work/self_referential_record.i"
"$minic" -S \
    "$work/self_referential_record.i" \
    -o "$work/self_referential_record.s"
grep -F ".globl main" "$work/self_referential_record.s" >/dev/null
printf '%s\n' "PASS compiler/c0/self_referential_record"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/function_pointer_record_fields.c" \
    -o "$work/function_pointer_record_fields.i"
"$minic" -S \
    "$work/function_pointer_record_fields.i" \
    -o "$work/function_pointer_record_fields.s"
grep -F ".globl main" "$work/function_pointer_record_fields.s" >/dev/null
printf '%s\n' "PASS compiler/c0/function_pointer_record_fields"

expect_failure() {
    name=$1
    diagnostic=$2

    "$host_cc" -E -P -x c \
        "$root/tests/compiler/c0/$name.c" \
        -o "$work/$name.i"
    if "$minic" -S "$work/$name.i" -o "$work/$name.s" \
        >"$work/$name.stdout" 2>"$work/$name.stderr"; then
        printf '%s\n' \
            "FAIL compiler/c0/$name: compilation unexpectedly succeeded" >&2
        exit 1
    fi
    grep -F "$diagnostic" "$work/$name.stderr" >/dev/null
    printf '%s\n' "PASS compiler/c0/$name"
}

expect_failure \
    invalid_pointer_member_base \
    "pointer member access requires a pointer to record"
expect_failure \
    invalid_pointer_member_name \
    "record has no such member"
expect_failure \
    invalid_const_pointer_member_assignment \
    "assignment target must be a modifiable lvalue"
expect_failure \
    invalid_self_record_by_value \
    "record field cannot use incomplete type by value"
expect_failure \
    invalid_unknown_record_pointer \
    "use of undeclared record tag"
