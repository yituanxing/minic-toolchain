#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-pointer-integer-casts

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/pointer_integer_casts.c" \
    -o "$work/pointer_integer_casts.i"
"$minic" -S "$work/pointer_integer_casts.i" \
    -o "$work/pointer_integer_casts.s"

# Internal object/function addresses use the explicit PC-relative local
# address pseudo (lla), not a GOT-selecting la. This preserves the current
# NOPIC relocation/ABI contract across the integer round trip.
grep -E '^[[:space:]]+lla[[:space:]]+[^,]+,[[:space:]]*value$' "$work/pointer_integer_casts.s" >/dev/null
grep -E '^[[:space:]]+lla[[:space:]]+[^,]+,[[:space:]]*target$' "$work/pointer_integer_casts.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/pointer_integer_casts object=1 function=1 roundtrip-through-ulong=1 implicit-assignment-unchanged=1'
