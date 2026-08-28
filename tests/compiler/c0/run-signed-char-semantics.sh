#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-signed-char

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/signed_char_semantics.c" \
    -o "$work/signed_char_semantics.i"
"$minic" -S "$work/signed_char_semantics.i" -o "$work/signed_char_semantics.s"

grep -E '^[[:space:]]+lb[[:space:]]+[^,]+,[[:space:]]*0\([^)]*\)$' "$work/signed_char_semantics.s" >/dev/null
grep -E '^[[:space:]]+lbu[[:space:]]+[^,]+,[[:space:]]*0\([^)]*\)$' "$work/signed_char_semantics.s" >/dev/null
grep -E '^[[:space:]]+sb[[:space:]]+[^,]+,[[:space:]]*0\([^)]*\)$' "$work/signed_char_semantics.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/signed_char_semantics signed-load=lb unsigned-and-plain-load=lbu store=sb'
