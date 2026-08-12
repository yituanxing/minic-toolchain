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

for name in invalid_direct_incompatible_function_pointer_call \
            invalid_void_pointer_variable_function_call; do
    "$host_cc" -E -P -x c "$root/tests/compiler/c0/$name.c" -o "$work/$name.i"
    if "$minic" -S "$work/$name.i" -o "$work/$name.s" \
        >"$work/$name.stdout" 2>"$work/$name.stderr"; then
        printf '%s\n' "FAIL compiler/c0/$name: compilation unexpectedly succeeded" >&2
        exit 1
    fi
    grep -F 'call argument type does not match declaration' "$work/$name.stderr" >/dev/null
    printf '%s\n' "PASS compiler/c0/$name"
done
