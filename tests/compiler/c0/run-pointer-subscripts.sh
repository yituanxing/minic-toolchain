#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-pointer-subscripts

mkdir -p "$work"

compile_success() {
    name=$1
    source=$2

    "$host_cc" -E -P -x c "$source" -o "$work/$name.i"
    "$minic" -S "$work/$name.i" -o "$work/$name.s"
}

compile_success \
    pointer_subscript \
    "$root/tests/programs/c0/pointer_subscript.c"
grep -F "  slli a0, a0, 2" "$work/pointer_subscript.s" >/dev/null
grep -F "  lw a0, 0(a0)" "$work/pointer_subscript.s" >/dev/null
grep -E '^  sw t0, 0\((a0|t1)\)$' "$work/pointer_subscript.s" >/dev/null
grep -F "  call read_at" "$work/pointer_subscript.s" >/dev/null
printf '%s\n' "PASS compiler/c0/pointer_subscript"

compile_success \
    pointer_subscript_const \
    "$root/tests/compiler/c0/pointer_subscript_const.c"
grep -F ".type read_const, @function" \
    "$work/pointer_subscript_const.s" >/dev/null
grep -F "  lw a0, 0(a0)" "$work/pointer_subscript_const.s" >/dev/null
printf '%s\n' "PASS compiler/c0/pointer_subscript_const"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/invalid_scalar_subscript.c" \
    -o "$work/invalid_scalar_subscript.i"
if "$minic" -S \
    "$work/invalid_scalar_subscript.i" \
    -o "$work/invalid_scalar_subscript.s" \
    >"$work/invalid_scalar_subscript.stdout" \
    2>"$work/invalid_scalar_subscript.stderr"; then
    printf '%s\n' \
        "FAIL compiler/c0/invalid_scalar_subscript: compilation unexpectedly succeeded" >&2
    exit 1
fi
grep -F "subscript base must be an array or pointer" \
    "$work/invalid_scalar_subscript.stderr" >/dev/null
printf '%s\n' "PASS compiler/c0/invalid_scalar_subscript"
