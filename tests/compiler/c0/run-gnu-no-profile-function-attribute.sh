#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-no-profile-function-attribute

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -std=gnu11 -x c \
  "$root/tests/compiler/c0/gnu_no_profile_function_attribute.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
test -s "$work/output.s"
grep -F 'no_profile_decl:' "$work/output.s" >/dev/null
grep -F '.noinstr.text' "$work/output.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/gnu_no_profile_function_attribute parse-only=1 section=preserved'
