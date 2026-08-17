#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-function-pointer-bridge-call

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/gnu_function_pointer_bridge_call.c" \
    -o "$work/bridge.i"
"$minic" -S "$work/bridge.i" -o "$work/bridge.s"
grep -F '  call __cpuhp_setup_state' "$work/bridge.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/gnu_function_pointer_bridge_call explicit-void-bridge=2 target-bitcast=1 direct-incompatible=still-strict'

"$host_cc" -E -P -x c "$root/tests/compiler/c0/gnu_function_pointer_to_void_call.c" \
    -o "$work/function-to-void.i"
"$minic" -S "$work/function-to-void.i" -o "$work/function-to-void.s"
test "$(grep -c -F '  call dereference_symbol_descriptor' "$work/function-to-void.s")" -ge 2
grep -F '  call through_function_pointer' "$work/function-to-void.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/gnu_function_pointer_to_void_call direct-function=1 function-pointer-expression=1 target-void-pointer=1'

"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/gnu_void_pointer_variable_function_argument.c" \
    -o "$work/void-pointer-argument.i"
"$minic" -S "$work/void-pointer-argument.i" -o "$work/void-pointer-argument.s"
grep -F 'bridge:' "$work/void-pointer-argument.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/gnu_void_pointer_variable_function_argument assignment-conversion=1'

name=invalid_direct_incompatible_function_pointer_call
"$host_cc" -E -P -x c "$root/tests/compiler/c0/$name.c" -o "$work/$name.i"
if "$minic" -S "$work/$name.i" -o "$work/$name.s" \
    >"$work/$name.stdout" 2>"$work/$name.stderr"; then
    printf '%s\n' "FAIL compiler/c0/$name: compilation unexpectedly succeeded" >&2
    exit 1
fi
grep -F 'call argument type does not match declaration' "$work/$name.stderr" >/dev/null
printf '%s\n' "PASS compiler/c0/$name"

"$host_cc" -E -P -std=gnu11 -x c \
    "$root/tests/compiler/c0/gnu_function_void_pointer_equality.c" \
    -o "$work/function-void-equality.i"
"$minic" -S "$work/function-void-equality.i" -o "$work/function-void-equality.s"
grep -F 'main:' "$work/function-void-equality.s" >/dev/null
printf '%s\n' 'PASS compiler/c0/gnu_function_void_pointer_equality bidirectional=1 direct-function=1'
