#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug/gnu-extern-void-symbol"}
mkdir -p "$work"

preprocess() {
    name=$1
    "$host_cc" -E -P -x c "$root/tests/compiler/c0/$name.c" -o "$work/$name.i"
}

expect_failure() {
    name=$1
    expected=${2:-}
    preprocess "$name"
    if "$minic" -S "$work/$name.i" -o "$work/$name.s" >"$work/$name.stdout" 2>"$work/$name.stderr"; then
        echo "FAIL compiler/c0/$name: compilation unexpectedly succeeded" >&2
        exit 1
    fi
    if test -n "$expected" && ! grep -F "$expected" "$work/$name.stderr" >/dev/null; then
        echo "FAIL compiler/c0/$name: diagnostic mismatch" >&2
        cat "$work/$name.stderr" >&2
        exit 1
    fi
}

preprocess gnu_extern_void_symbol
"$minic" -S "$work/gnu_extern_void_symbol.i" -o "$work/gnu_extern_void_symbol.s"
grep -F "  la a0, __nosave_begin" "$work/gnu_extern_void_symbol.s" >/dev/null
grep -F "  la a0, __nosave_end" "$work/gnu_extern_void_symbol.s" >/dev/null
if grep -F ".type __nosave_begin, @object" "$work/gnu_extern_void_symbol.s" >/dev/null || \
   grep -F ".type __nosave_end, @object" "$work/gnu_extern_void_symbol.s" >/dev/null || \
   grep -F "__nosave_begin:" "$work/gnu_extern_void_symbol.s" >/dev/null || \
   grep -F "__nosave_end:" "$work/gnu_extern_void_symbol.s" >/dev/null; then
    echo "FAIL compiler/c0/gnu_extern_void_symbol: extern-only symbol emitted storage" >&2
    exit 1
fi
preprocess gnu_extern_void_sizeof
"$minic" -S "$work/gnu_extern_void_sizeof.i" -o "$work/gnu_extern_void_sizeof.s"
grep -F "  li a0, 1" "$work/gnu_extern_void_sizeof.s" >/dev/null
expect_failure invalid_void_object_definition
expect_failure invalid_extern_void_redeclaration "conflicting extern object redeclaration"
printf '%s\n' "PASS compiler/c0/gnu_extern_void_symbol extern-only=1 opaque-void=1 multi-declarator=2 address=rv64-la storage=none sizeof=gnu-byte definition=reject redeclaration=reject"
