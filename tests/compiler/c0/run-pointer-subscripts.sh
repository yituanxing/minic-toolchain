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
grep -E '^[[:space:]]+lw[[:space:]]+[^,]+,[[:space:]]*0\([^)]*\)$' "$work/pointer_subscript.s" >/dev/null
grep -E '^[[:space:]]+sw[[:space:]]+[^,]+,[[:space:]]*0\([^)]*\)$' "$work/pointer_subscript.s" >/dev/null
grep -F "  call read_at" "$work/pointer_subscript.s" >/dev/null
printf '%s\n' "PASS compiler/c0/pointer_subscript"

compile_success \
    pointer_subscript_const \
    "$root/tests/compiler/c0/pointer_subscript_const.c"
grep -F ".type read_const, @function" \
    "$work/pointer_subscript_const.s" >/dev/null
# The const pointer parameter has no natural 4-byte alignment guarantee in
# Core.  A wide LW would be unsafe; the current target reconstructs the exact
# 32-bit value from four unsigned byte lanes, shifts and ORs.
for offset in 0 1 2 3; do
    grep -E "^[[:space:]]+lbu[[:space:]]+[^,]+,[[:space:]]*${offset}\\([^)]*\\)$" \
        "$work/pointer_subscript_const.s" >/dev/null
done
for shift in 8 16 24; do
    grep -E "^[[:space:]]+slli[[:space:]]+[^,]+,[[:space:]]*[^,]+,[[:space:]]*${shift}$" \
        "$work/pointer_subscript_const.s" >/dev/null
done
test "$(grep -Ec '^[[:space:]]+or[[:space:]]+' "$work/pointer_subscript_const.s")" -ge 3
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
