#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-unsigned

mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/unsigned_declarations.c" \
    -o "$work/unsigned_declarations.i"
"$minic" -S \
    "$work/unsigned_declarations.i" \
    -o "$work/unsigned_declarations.s"

grep -F ".globl main" "$work/unsigned_declarations.s" >/dev/null
grep -F ".Lmain_return:" "$work/unsigned_declarations.s" >/dev/null
grep -F "  mv s0, sp" "$work/unsigned_declarations.s" >/dev/null
printf '%s\n' "PASS compiler/c0/unsigned_declarations"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/invalid_duplicate_unsigned_local.c" \
    -o "$work/invalid_duplicate_unsigned_local.i"
if "$minic" -S \
    "$work/invalid_duplicate_unsigned_local.i" \
    -o "$work/invalid_duplicate_unsigned_local.s" \
    >"$work/invalid_duplicate_unsigned_local.stdout" \
    2>"$work/invalid_duplicate_unsigned_local.stderr"; then
    printf '%s\n' \
        "FAIL compiler/c0/invalid_duplicate_unsigned_local: compilation unexpectedly succeeded" >&2
    exit 1
fi
grep -F "duplicate local declaration" \
    "$work/invalid_duplicate_unsigned_local.stderr" >/dev/null
printf '%s\n' "PASS compiler/c0/invalid_duplicate_unsigned_local"
