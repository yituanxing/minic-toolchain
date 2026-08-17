#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-cc}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0/pointer-equality

rm -rf "$work"
mkdir -p "$work"

compile_failure() {
    name=$1
    expected=$2

    "$host_cc" -E -P -x c "$root/tests/compiler/c0/$name.c" -o "$work/$name.i"
    if "$minic" -S "$work/$name.i" -o "$work/$name.s" \
        >"$work/$name.stdout" 2>"$work/$name.stderr"; then
        printf '%s\n' "FAIL compiler/c0/$name: compilation unexpectedly succeeded" >&2
        exit 1
    fi
    if ! grep -F "$expected" "$work/$name.stderr" >/dev/null; then
        printf '%s\n' "FAIL compiler/c0/$name: expected diagnostic: $expected" >&2
        cat "$work/$name.stderr" >&2
        exit 1
    fi
    printf '%s\n' "PASS compiler/c0/$name"
}

name=pointer_equality
"$host_cc" -E -P -x c "$root/tests/programs/c0/$name.c" -o "$work/$name.i"
"$minic" -S "$work/$name.i" -o "$work/$name.s"
grep -F '  seqz a0, a0' "$work/$name.s" >/dev/null
grep -F '  snez a0, a0' "$work/$name.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/pointer_equality lowering=xor-seqz-snez'

name=gnu_void_pointer_function_member_assignment
"$host_cc" -fsyntax-only -std=gnu11 -Werror -Wno-pedantic -x c \
    "$root/tests/compiler/c0/$name.c"
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/$name.c" -o "$work/$name.i"
"$minic" -S "$work/$name.i" -o "$work/$name.s"
grep -F 'assign_object_pointer:' "$work/$name.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/gnu_void_pointer_function_member_assignment assignment-conversion=1'

name=gnu_function_pointer_void_comparison
"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/$name.c" -o "$work/$name.i"
"$minic" -S "$work/$name.i" -o "$work/$name.s"
grep -F 'compare_object_pointer:' "$work/$name.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/gnu_function_pointer_void_comparison equality=gnu-extension member-function-pointer=1'

compile_failure invalid_pointer_equality_nonzero_integer 'binary operator requires int operands'
compile_failure invalid_pointer_equality_incompatible 'binary operator requires int operands'
