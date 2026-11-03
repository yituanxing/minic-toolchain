#!/bin/sh
set -eu
: "${MINIC_CC:?point MINIC_CC at the built compiler}"
host=${HOST_CC:-cc}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
src=tests/compiler/c0/core_overflow_enum_narrow.c

"$host" -E -P -nostdinc -x c "$src" -o "$work/positive.i"
"$MINIC_CC" --target=aarch64-linux -fPIE -S "$work/positive.i" \
    -o "$work/positive.s"
test -s "$work/positive.s"
for kind in POINTER PREDICATE; do
    # Both GNU forms are distinct C admission decisions. Check them
    # independently so the first parse rejection cannot mask the second.
    "$host" -E -P -nostdinc -x c "-DMINIC_OVERFLOW_ENUM_${kind}_NEGATIVE" \
        "$src" -o "$work/enum-${kind}.i"
    if "$host" -std=gnu11 -fsyntax-only -x c "$work/enum-${kind}.i" \
        >"$work/gnu-${kind}.log" 2>&1; then
        echo "CORE_OVERFLOW_ENUM_ORIGIN=FAIL GNU accepted ${kind} enum result" >&2
        exit 1
    fi
    if "$MINIC_CC" --target=aarch64-linux -fPIE -S "$work/enum-${kind}.i" \
        -o "$work/enum-${kind}.s" >"$work/minic-${kind}.log" 2>&1; then
        echo "CORE_OVERFLOW_ENUM_ORIGIN=FAIL MiniC admitted ${kind} enum result" >&2
        exit 1
    fi
    test ! -e "$work/enum-${kind}.s"
done
echo 'CORE_OVERFLOW_ENUM_ORIGIN=PASS AArch64_char_positive=1 enum_pointer_and_predicate_negative=2 GNU_oracle=reject'
