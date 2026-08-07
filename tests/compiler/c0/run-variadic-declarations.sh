#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0/variadic-declarations

rm -rf "$work"
mkdir -p "$work"

compile_success() {
    name=$1

    "$host_cc" -E -P -x c \
        "$root/tests/compiler/c0/$name.c" \
        -o "$work/$name.i"
    "$minic" -S "$work/$name.i" -o "$work/$name.s"
    grep -F '.globl main' "$work/$name.s" >/dev/null
    printf '%s\n' "PASS compiler/c0/$name"
}

compile_failure() {
    name=$1
    expected=$2

    "$host_cc" -E -P -x c \
        "$root/tests/compiler/c0/$name.c" \
        -o "$work/$name.i"
    if "$minic" -S "$work/$name.i" -o "$work/$name.s" \
        >"$work/$name.stdout" 2>"$work/$name.stderr"; then
        printf '%s\n' "FAIL compiler/c0/$name: compilation unexpectedly succeeded" >&2
        exit 1
    fi
    grep -F "$expected" "$work/$name.stderr" >/dev/null
    printf '%s\n' "PASS compiler/c0/$name"
}

compile_success variadic_declaration
compile_failure invalid_variadic_without_fixed 'ellipsis requires at least one fixed parameter'
compile_failure invalid_variadic_conflict 'conflicting function declaration'
compile_failure invalid_variadic_definition 'variadic function definitions are not supported yet'
compile_failure invalid_variadic_function_pointer_field \
    'variadic function pointer fields are not supported yet'
