#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-void-function-pointer-assignment

rm -rf "$work"
mkdir -p "$work"
"$host_cc" -fsyntax-only -std=gnu11 -Werror -Wno-pedantic -x c \
  "$root/tests/compiler/c0/gnu_void_function_pointer_assignment.c"
"$host_cc" -E -P -std=gnu11 -x c \
  "$root/tests/compiler/c0/gnu_void_function_pointer_assignment.c" -o "$work/input.i"
"$minic" -S "$work/input.i" -o "$work/output.s"
test -s "$work/output.s"
grep -F 'linux_shaped_syscall_assignment:' "$work/output.s" >/dev/null
grep -F 'linux_shaped_early_assignment:' "$work/output.s" >/dev/null
grep -F 'gnu_function_pointer_to_void:' "$work/output.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/gnu_void_function_pointer_assignment both-directions=1 linux-shaped=1'
