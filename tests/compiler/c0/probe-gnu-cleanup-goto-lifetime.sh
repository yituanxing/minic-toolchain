#!/bin/sh
set -eu

: "${MINIC_CC:?MINIC_CC must name the compiler under test}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

# Compare actual machine execution, not just successful .s production.
cc -std=gnu11 -O0 "$root/tests/compiler/c0/gnu_cleanup_runtime.c" \
   -o "$work/reference"
"$work/reference"
"$MINIC_CC" --target=x86_64-linux -S \
    "$root/tests/compiler/c0/gnu_cleanup_runtime.c" -o "$work/minic.s"
cc -no-pie "$work/minic.s" -o "$work/minic"
"$work/minic"

# A goto may leave an active GNU cleanup lifetime, but cannot enter one.
if "$MINIC_CC" --target=x86_64-linux -S \
    "$root/tests/compiler/c0/gnu_cleanup_goto_invalid.c" \
    -o "$work/invalid.s" 2>"$work/invalid.log"; then
    echo 'MINIC_GNU_GOTO_CLEANUP=FAIL entered_cleanup_scope' >&2
    exit 1
fi
grep -Fq 'goto cannot enter a different GNU cleanup lifetime' "$work/invalid.log"
test ! -e "$work/invalid.s"
if "$MINIC_CC" --target=x86_64-linux -S \
    "$root/tests/compiler/c0/gnu_cleanup_asm_goto_cross_scope.c" \
    -o "$work/invalid-asm-goto.s" 2>"$work/invalid-asm-goto.log"; then
    echo 'MINIC_GNU_GOTO_CLEANUP=FAIL asm_goto_cross_lifetime' >&2
    exit 1
fi
grep -Fq 'GNU asm goto cannot cross a GNU cleanup lifetime' "$work/invalid-asm-goto.log"
test ! -e "$work/invalid-asm-goto.s"
echo 'MINIC_GNU_GOTO_CLEANUP=PASS native_cleanup_inline_asm=1 invalid_entry_rejected=1 invalid_asm_cross=1'
