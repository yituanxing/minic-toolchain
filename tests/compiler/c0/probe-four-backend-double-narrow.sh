#!/bin/sh
# Dual-ISA fast GNU/QEMU differential: four valid floating-to-narrow casts.
set -eu
test "$#" -eq 4
: "${MINIC_CC:?set MINIC_CC}"
target=$1; gcc=$2; qemu=$3; sysroot=$4
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
src="$root/tests/compiler/c0/double_to_narrow_integer.c"
"$gcc" -O0 -std=gnu11 "$src" -o "$work/gnu.elf"
"$gcc" -E -P -std=gnu11 "$src" -o "$work/source.i"
timeout 40s "$MINIC_CC" --target="$target-linux" -S "$work/source.i" -o "$work/mini.s"
"$gcc" "$work/mini.s" -o "$work/mini.elf"
timeout 20s "$qemu" -L "$sysroot" "$work/gnu.elf" >"$work/gnu.out" 2>"$work/gnu.err"
timeout 20s "$qemu" -L "$sysroot" "$work/mini.elf" >"$work/mini.out" 2>"$work/mini.err"
if ! cmp -s "$work/gnu.out" "$work/mini.out" ||
   ! cmp -s "$work/gnu.err" "$work/mini.err" ||
   ! grep -qx -- '-12 240 -1234 40000' "$work/mini.out"; then
  echo "FOUR_BACKEND_DOUBLE_NARROW=FAIL target=$target stage=QEMU_GNU_DIFFERENTIAL" >&2
  tail -n 15 "$work/gnu.err" "$work/mini.err" >&2 || true
  exit 1
fi
echo "FOUR_BACKEND_DOUBLE_NARROW=PASS target=$target signed_char=1 unsigned_char=1 signed_short=1 unsigned_short=1 GNU_QEMU_byte_exact=1"
