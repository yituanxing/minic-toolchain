#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
rv_cc=${RV_CC:-riscv64-linux-gnu-gcc}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-local-fixed-register-bindings

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/gnu_local_fixed_register_bindings.c" \
    -o "$work/gnu_local_fixed_register_bindings.i"
"$minic" -S "$work/gnu_local_fixed_register_bindings.i" \
    -o "$work/gnu_local_fixed_register_bindings.s"

test -s "$work/gnu_local_fixed_register_bindings.s"
grep -F 'add a0, a0, a1 # nr=a7' "$work/gnu_local_fixed_register_bindings.s" >/dev/null
grep -F 'mv a0, a0' "$work/gnu_local_fixed_register_bindings.s" >/dev/null
"$rv_cc" -c "$work/gnu_local_fixed_register_bindings.s" \
    -o "$work/gnu_local_fixed_register_bindings.o"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/gnu_local_fixed_register_binding_reject.c" \
    -o "$work/gnu_local_fixed_register_binding_reject.i"
set +e
"$minic" -S "$work/gnu_local_fixed_register_binding_reject.i" \
    -o "$work/gnu_local_fixed_register_binding_reject.s" \
    >"$work/reject.stdout" 2>"$work/reject.stderr"
status=$?
set -e
test "$status" -ne 0
grep -F 'local fixed register binding is not supported by this target' \
    "$work/reject.stderr" >/dev/null

printf '%s\n' 'PASS compiler/c0/gnu_local_fixed_register_bindings scope=local storage=ordinary-local asm-binding=a0,a1,a7 overlap=a0 output+input target-policy=local-only'
