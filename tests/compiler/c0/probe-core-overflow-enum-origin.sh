#!/bin/sh
set -eu
: "${MINIC_CC:?point MINIC_CC at the built compiler}"
host=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
src=tests/compiler/c0/core_overflow_enum_narrow.c

"$host" -E -P -nostdinc -x c "$src" -o "$work/positive.i"
"$host" -E -P -nostdinc -x c -DMINIC_OVERFLOW_ENUM_NEGATIVE \
    "$src" -o "$work/enum-negative.i"

# GNU rejects enum-pointer checked destinations even when mode(byte) gives
# the enum an i8 execution representation. The Core numeric ABI must not
# allow that source invalidity through an integer-width-only classifier.
if "$host" -std=gnu11 -fsyntax-only -x c "$work/enum-negative.i" \
    >"$work/gnu-negative.log" 2>&1; then
    echo 'CORE_OVERFLOW_ENUM_ORIGIN=FAIL GNU accepted enum result' >&2
    exit 1
fi

"$MINIC_CC" --target=aarch64-linux -fPIE -S "$work/positive.i" \
    -o "$work/positive.s"
test -s "$work/positive.s"
if "$MINIC_CC" --target=aarch64-linux -fPIE -S "$work/enum-negative.i" \
    -o "$work/enum-negative.s" >"$work/minic-negative.log" 2>&1; then
    echo 'CORE_OVERFLOW_ENUM_ORIGIN=FAIL MiniC admitted enum result' >&2
    exit 1
fi
test ! -e "$work/enum-negative.s"
echo 'CORE_OVERFLOW_ENUM_ORIGIN=PASS AArch64_char_positive=1 enum_pointer_and_predicate_negative=2 GNU_oracle=reject'
