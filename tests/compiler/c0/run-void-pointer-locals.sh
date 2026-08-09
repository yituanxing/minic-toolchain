#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-void-pointer-locals

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/void_pointer_local.c" -o "$work/void_pointer_local.i"
"$minic" -S "$work/void_pointer_local.i" -o "$work/void_pointer_local.s"
grep -F '.globl main' "$work/void_pointer_local.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/void_pointer_local forms=void*,const-void*'

"$host_cc" -E -P -x c "$root/tests/compiler/c0/invalid_void_local_object.c" \
    -o "$work/invalid_void_local_object.i"
if "$minic" -S "$work/invalid_void_local_object.i" -o "$work/invalid_void_local_object.s" \
    >"$work/invalid_void_local_object.stdout" 2>"$work/invalid_void_local_object.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/invalid_void_local_object: compilation unexpectedly succeeded' >&2
    exit 1
fi
grep -F 'local object cannot have void type' "$work/invalid_void_local_object.stderr" >/dev/null
printf '%s\n' 'PASS compiler/c0/invalid_void_local_object'
