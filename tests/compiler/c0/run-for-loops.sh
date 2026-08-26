#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-for-loops

mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/programs/c0/for_loop.c" \
    -o "$work/for_loop.i"
"$minic" -S "$work/for_loop.i" -o "$work/for_loop.s"

require_fixed() {
    pattern=$1
    if ! grep -F "$pattern" "$work/for_loop.s" >/dev/null; then
        printf '%s\n' "FAIL compiler/c0/for_loop_lowering: missing fixed pattern: $pattern" >&2
        cat "$work/for_loop.s" >&2
        exit 1
    fi
}

require_regex() {
    pattern=$1
    label=$2
    if ! grep -E "$pattern" "$work/for_loop.s" >/dev/null; then
        printf '%s\n' "FAIL compiler/c0/for_loop_lowering: missing opcode: $label" >&2
        cat "$work/for_loop.s" >&2
        exit 1
    fi
}

# Core owns the CFG now. Keep this as a target-codegen contract without
# pinning the old AST emitter's labels or its choice of a0 as a scratch value.
require_fixed ".Lmain_core_bb"
require_fixed ".Lmain_core_return:"
require_regex '^[[:space:]]+sltu[[:space:]]' sltu
require_regex '^[[:space:]]+addw[[:space:]]' addw
require_regex '^[[:space:]]+lwu[[:space:]]' lwu
printf '%s\n' "PASS compiler/c0/for_loop_lowering"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/for_empty_initializer.c" \
    -o "$work/for_empty_initializer.i"
"$minic" -S "$work/for_empty_initializer.i" -o "$work/for_empty_initializer.s"
printf '%s\n' "PASS compiler/c0/for_empty_initializer"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/for_assignment_update.c" \
    -o "$work/for_assignment_update.i"
"$minic" -S "$work/for_assignment_update.i" -o "$work/for_assignment_update.s"
printf '%s\n' "PASS compiler/c0/for_assignment_update"

expect_failure() {
    name=$1
    message=$2

    "$host_cc" -E -P -x c \
        "$root/tests/compiler/c0/$name.c" \
        -o "$work/$name.i"
    if "$minic" -S "$work/$name.i" -o "$work/$name.s" \
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

expect_failure \
    invalid_for_update \
    "assignment expression requires a modifiable object lvalue"
expect_failure \
    invalid_for_increment_target \
    "prefix update requires a modifiable scalar lvalue"
