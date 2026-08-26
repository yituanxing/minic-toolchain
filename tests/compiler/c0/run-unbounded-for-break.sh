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

# Core owns loop CFG construction.  The source contains two unbounded for (;;)
# loops and exactly two explicit if statements.  Therefore target assembly must
# contain Core blocks/return and exactly two conditional branches: an empty for
# condition must not synthesize an additional guard branch.
grep -F ".Lmain_core_bb" "$work/unbounded_for_break.s" >/dev/null
grep -F ".Lmain_core_return:" "$work/unbounded_for_break.s" >/dev/null
branch_count=$(grep -Ec '^[[:space:]]+beqz[[:space:]]' "$work/unbounded_for_break.s" || true)
if [ "$branch_count" -ne 2 ]; then
    printf '%s\n' \
        "FAIL compiler/c0/unbounded_for_break: expected exactly two source-if guards, got $branch_count" >&2
    cat "$work/unbounded_for_break.s" >&2
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
