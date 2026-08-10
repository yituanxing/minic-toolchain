#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-switch-control-flow

mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/programs/c0/switch_control_flow.c" \
    -o "$work/switch_control_flow.i"
"$minic" -S \
    "$work/switch_control_flow.i" \
    -o "$work/switch_control_flow.s"

grep -F ".Lswitch_case_" "$work/switch_control_flow.s" >/dev/null
grep -F ".Lswitch_default_" "$work/switch_control_flow.s" >/dev/null
grep -F "  j .Lswitch_end_" "$work/switch_control_flow.s" >/dev/null
grep -F "  j .Lwhile_end_" "$work/switch_control_flow.s" >/dev/null
printf '%s\n' "PASS compiler/c0/switch_control_flow_lowering"

expect_failure() {
    name=$1
    message=$2

    "$host_cc" -E -P -x c \
        "$root/tests/compiler/c0/$name.c" \
        -o "$work/$name.i"
    if "$minic" -S "$work/$name.i" -o "$work/$name.s" \
        >"$work/$name.stdout" 2>"$work/$name.stderr"; then
        printf '%s\n' \
            "FAIL compiler/c0/$name: compilation unexpectedly succeeded" >&2
        exit 1
    fi
    if ! grep -F "$message" "$work/$name.stderr" >/dev/null; then
        printf '%s\n' "FAIL compiler/c0/$name: diagnostic mismatch" >&2
        cat "$work/$name.stderr" >&2
        exit 1
    fi
    printf '%s\n' "PASS compiler/c0/$name"
}

expect_failure \
    invalid_case_outside_switch \
    "case label requires an enclosing switch"
expect_failure \
    invalid_default_outside_switch \
    "default label requires an enclosing switch"
expect_failure \
    invalid_duplicate_case \
    "duplicate case value"
expect_failure \
    invalid_duplicate_default \
    "duplicate default label"
expect_failure \
    invalid_switch_double_selector \
    "switch selector requires an integer expression"
expect_failure \
    invalid_case_nonconstant \
    "case label currently requires an integer constant expression"
