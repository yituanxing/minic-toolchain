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
    "$root/tests/programs/c0/plain_char_semantics.c" \
    -o "$work/plain_char_semantics.i"
"$minic" -S \
    "$work/plain_char_semantics.i" \
    -o "$work/plain_char_semantics.s"
grep -F "  .byte 128" "$work/plain_char_semantics.s" >/dev/null
grep -F "  .byte 255" "$work/plain_char_semantics.s" >/dev/null
grep -F "  lbu " "$work/plain_char_semantics.s" >/dev/null
grep -F "  sb " "$work/plain_char_semantics.s" >/dev/null
grep -F "  andi a0, a0, 255" "$work/plain_char_semantics.s" >/dev/null
if grep -F "  lb " "$work/plain_char_semantics.s" >/dev/null; then
    printf '%s\n' \
        "FAIL compiler/c0/plain_char_semantics: RV64 plain char used signed byte load" >&2
    exit 1
fi
printf '%s\n' "PASS compiler/c0/plain_char_semantics"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/hexadecimal_expression.c" \
    -o "$work/hexadecimal_expression.i"
"$minic" -S \
    "$work/hexadecimal_expression.i" \
    -o "$work/hexadecimal_expression.s"
grep -F "  li a0, 77" "$work/hexadecimal_expression.s" >/dev/null
printf '%s\n' "PASS compiler/c0/hexadecimal_expression"

MINIC="$minic" \
HOST_CC="$host_cc" \
BUILD_DIR="${BUILD_DIR:-"$root/build/debug"}" \
sh "$root/tests/compiler/c0/run-signed-char-semantics.sh"

expect_failure() {
    name=$1
    expected=$2

    "$host_cc" -E -P -x c \
        "$root/tests/compiler/c0/$name.c" \
        -o "$work/$name.i"
    if "$minic" -S \
        "$work/$name.i" \
        -o "$work/$name.s" \
        >"$work/$name.stdout" \
        2>"$work/$name.stderr"; then
        printf '%s\n' \
            "FAIL compiler/c0/$name: compilation unexpectedly succeeded" >&2
        exit 1
    fi
    grep -F "$expected" "$work/$name.stderr" >/dev/null
    printf '%s\n' "PASS compiler/c0/$name"
}

expect_failure invalid_plain_unsigned_char_pointer_assignment \
    "assignment expression type does not match target type"
