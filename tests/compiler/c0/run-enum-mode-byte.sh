#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/enum-mode-byte
mkdir -p "$work"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/enum_mode_byte.c" -o "$work/good.i"
"$minic" -S "$work/good.i" -o "$work/good.s"
test -s "$work/good.s"

"$host_cc" -E -P -std=gnu11 -x c "$root/tests/compiler/c0/enum_mode_byte_overflow.c" -o "$work/bad.i"
if "$minic" -S "$work/bad.i" -o "$work/bad.s" >"$work/bad.stdout" 2>"$work/bad.stderr"; then
  printf '%s\n' 'FAIL compiler/c0/enum-mode-byte: overflow unexpectedly succeeded' >&2
  exit 1
fi
grep -F 'enum mode(byte) cannot represent enumerator values' "$work/bad.stderr" >/dev/null

printf '%s\n' 'PASS compiler/c0/enum-mode-byte signed=1B unsigned=1B overflow=rejected'
