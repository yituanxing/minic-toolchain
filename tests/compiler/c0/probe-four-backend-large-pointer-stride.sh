#!/bin/sh
# Real GNU/QEMU differential: pointer offset by 4096-byte aggregate stride.
set -eu
test "$#" -eq 4
: "${MINIC_CC:?set MINIC_CC}"
target=$1
gcc=$2
qemu=$3
sysroot=$4
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
src="$root/tests/compiler/c0/large_pointer_stride.c"
"$gcc" -O0 -std=gnu11 "$src" -o "$work/gnu.elf"
timeout 40s "$MINIC_CC" --target="$target-linux" -S "$src" -o "$work/mini.s"
"$gcc" "$work/mini.s" -o "$work/mini.elf"
timeout 20s "$qemu" -L "$sysroot" "$work/gnu.elf" >"$work/gnu.out" 2>"$work/gnu.err"
timeout 20s "$qemu" -L "$sysroot" "$work/mini.elf" >"$work/mini.out" 2>"$work/mini.err"
if ! cmp -s "$work/gnu.out" "$work/mini.out" ||
   ! cmp -s "$work/gnu.err" "$work/mini.err" ||
   ! grep -qx 42 "$work/mini.out"; then
  echo "FOUR_BACKEND_LARGE_STRIDE=FAIL target=$target stage=QEMU_GNU_DIFFERENTIAL" >&2
  tail -n 12 "$work/gnu.err" "$work/mini.err" >&2 || true
  exit 1
fi
echo "FOUR_BACKEND_LARGE_STRIDE=PASS target=$target record_bytes=4096 GNU_QEMU_byte_exact=1"
