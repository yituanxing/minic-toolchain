#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-unsigned-64-literals
asm="$work/unsigned_64_literals.s"

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/unsigned_64_literals.c" \
    -o "$work/unsigned_64_literals.i"
"$minic" -S "$work/unsigned_64_literals.i" -o "$asm"

grep -F 'max_unsigned_64:' "$asm" >/dev/null
grep -F 'top_unsigned_bit:' "$asm" >/dev/null
grep -F 'unsigned_max_above_signed_max:' "$asm" >/dev/null
grep -F '  li a0, -1' "$asm" >/dev/null
grep -F '  srl a0, t0, a0' "$asm" >/dev/null
grep -F '  sltu a0, a0, t0' "$asm" >/dev/null
printf '%s\n' 'PASS compiler/c0/unsigned_64_literals max=UINT64_MAX bits=all-ones shift=logical comparison=unsigned'
