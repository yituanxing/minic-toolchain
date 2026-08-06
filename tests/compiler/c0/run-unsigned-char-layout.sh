#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-unsigned-char-layout

mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/programs/c0/unsigned_char_layout.c" \
    -o "$work/unsigned_char_layout.i"
"$minic" -S \
    "$work/unsigned_char_layout.i" \
    -o "$work/unsigned_char_layout.s"

grep -F "  .byte 128" "$work/unsigned_char_layout.s" >/dev/null
grep -F "  .byte 255" "$work/unsigned_char_layout.s" >/dev/null
grep -F ".size table, 4" "$work/unsigned_char_layout.s" >/dev/null
grep -F "  lbu " "$work/unsigned_char_layout.s" >/dev/null
grep -F "  sb " "$work/unsigned_char_layout.s" >/dev/null
grep -F "  andi a0, a0, 255" "$work/unsigned_char_layout.s" >/dev/null
if grep -F "  slli a0, a0, 2" "$work/unsigned_char_layout.s" >/dev/null; then
    printf '%s\n' \
        "FAIL compiler/c0/unsigned_char_layout: byte pointer used four-byte scaling" >&2
    exit 1
fi
printf '%s\n' "PASS compiler/c0/unsigned_char_layout"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/hexadecimal_expression.c" \
    -o "$work/hexadecimal_expression.i"
"$minic" -S \
    "$work/hexadecimal_expression.i" \
    -o "$work/hexadecimal_expression.s"
grep -F "  li a0, 77" "$work/hexadecimal_expression.s" >/dev/null
printf '%s\n' "PASS compiler/c0/hexadecimal_expression"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/invalid_plain_char.c" \
    -o "$work/invalid_plain_char.i"
if "$minic" -S \
    "$work/invalid_plain_char.i" \
    -o "$work/invalid_plain_char.s" \
    >"$work/invalid_plain_char.stdout" \
    2>"$work/invalid_plain_char.stderr"; then
    printf '%s\n' \
        "FAIL compiler/c0/invalid_plain_char: compilation unexpectedly succeeded" >&2
    exit 1
fi
grep -F "expected type name" "$work/invalid_plain_char.stderr" >/dev/null
printf '%s\n' "PASS compiler/c0/invalid_plain_char"
