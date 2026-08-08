#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-unbounded-for-break

mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/unbounded_for_break.c" \
    -o "$work/unbounded_for_break.i"
"$minic" -S \
    "$work/unbounded_for_break.i" \
    -o "$work/unbounded_for_break.s"

grep -F ".Lwhile_condition_0:" "$work/unbounded_for_break.s" >/dev/null
grep -F ".Lwhile_condition_1:" "$work/unbounded_for_break.s" >/dev/null
grep -F "  j .Lwhile_end_0" "$work/unbounded_for_break.s" >/dev/null
grep -F "  j .Lwhile_end_1" "$work/unbounded_for_break.s" >/dev/null
if grep -F "beqz a0, .Lwhile_end_" \
    "$work/unbounded_for_break.s" >/dev/null; then
    printf '%s\n' \
        "FAIL compiler/c0/unbounded_for_break: empty conditions emitted loop guards" >&2
    exit 1
fi
printf '%s\n' "PASS compiler/c0/unbounded_for_break"

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
    grep -F "$message" "$work/$name.stderr" >/dev/null
    printf '%s\n' "PASS compiler/c0/$name"
}

expect_failure \
    invalid_break_outside_loop \
    "break statement requires an enclosing loop or switch"
expect_failure \
    invalid_break_missing_semicolon \
    "expected ';' after break"
expect_failure \
    invalid_empty_while_condition \
    "expected expression"
