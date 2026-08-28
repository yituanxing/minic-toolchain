#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-postfix-subscripts

mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/programs/c0/postfix_subscript.c" \
    -o "$work/postfix_subscript.i"
"$minic" -S \
    "$work/postfix_subscript.i" \
    -o "$work/postfix_subscript.s"
grep -F ".Lexercise_core_bb" "$work/postfix_subscript.s" >/dev/null
grep -F ".Lexercise_core_return:" "$work/postfix_subscript.s" >/dev/null
# The semantic owner is multidimensional pointer stride.  Core materializes
# both the 12-byte Row stride and the 4-byte int stride through multiply rather
# than requiring the legacy emitter's slli strength reduction.
grep -E '^[[:space:]]+li[[:space:]]+[^,]+,[[:space:]]*12$' \
    "$work/postfix_subscript.s" >/dev/null
grep -E '^[[:space:]]+mul[[:space:]]+' "$work/postfix_subscript.s" >/dev/null
grep -E '^[[:space:]]+sw[[:space:]]+[^,]+,[[:space:]]*0\([^)]*\)$' \
    "$work/postfix_subscript.s" >/dev/null
printf '%s\n' "PASS compiler/c0/postfix_subscript normalized=core-pointer-stride"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/invalid_postfix_subscript_scalar.c" \
    -o "$work/invalid_postfix_subscript_scalar.i"
if "$minic" -S \
    "$work/invalid_postfix_subscript_scalar.i" \
    -o "$work/invalid_postfix_subscript_scalar.s" \
    >"$work/invalid_postfix_subscript_scalar.stdout" \
    2>"$work/invalid_postfix_subscript_scalar.stderr"; then
    printf '%s\n' \
        "FAIL compiler/c0/invalid_postfix_subscript_scalar: compilation unexpectedly succeeded" >&2
    exit 1
fi
grep -F "subscript base must be an array or pointer" \
    "$work/invalid_postfix_subscript_scalar.stderr" >/dev/null
printf '%s\n' "PASS compiler/c0/invalid_postfix_subscript_scalar"
