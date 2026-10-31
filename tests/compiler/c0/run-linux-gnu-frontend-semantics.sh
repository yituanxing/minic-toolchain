#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic-cc"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-linux-gnu-frontend-semantics
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -fshort-wchar -x c \
  "$root/tests/compiler/c0/linux_gnu_frontend_semantics.c" -o "$work/test.i"
"$minic" --target=x86_64-linux -fshort-wchar -funsigned-char -S \
  "$work/test.i" -o "$work/test.s"
"$host_cc" -no-pie "$work/test.s" -o "$work/test"
"$work/test"
grep -Fq '.section .rodata.gnu.front' "$work/test.s"
printf '%s\n' 'PASS compiler/c0/linux_gnu_frontend union-cast=1 function-decay=1 array-pointer-cast=1 zero-stack-sizeof=1 static-section=1 inferred-record-array=1'
