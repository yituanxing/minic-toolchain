#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-stack-fixed-arguments

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c \
    "$root/tests/compiler/c0/stack_fixed_arguments.c" \
    -o "$work/stack_fixed_arguments.i"
"$minic" -S \
    "$work/stack_fixed_arguments.i" \
    -o "$work/stack_fixed_arguments.s"

grep -F '.type minic_abi_sum9, @function' "$work/stack_fixed_arguments.s" >/dev/null
grep -F '  call gcc_abi_sum9' "$work/stack_fixed_arguments.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/stack_fixed_arguments fixed=9 integer-class register=8 stack=1'
