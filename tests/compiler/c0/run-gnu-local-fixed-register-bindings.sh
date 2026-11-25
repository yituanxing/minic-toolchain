#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
rv_cc=${RV_CC:-riscv64-linux-gnu-gcc}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-local-fixed-register-bindings
rm -rf "$work"
mkdir -p "$work"
"$host_cc" -E -P -x c "$root/tests/compiler/c0/gnu_local_fixed_register_bindings.c" -o "$work/ok.i"
"$minic" -S "$work/ok.i" -o "$work/ok.s"
grep -F 'add a0, a0, a1 # nr=a7' "$work/ok.s" >/dev/null
grep -F 'mv a0, a0' "$work/ok.s" >/dev/null
"$rv_cc" -c "$work/ok.s" -o "$work/ok.o"
"$host_cc" -E -P -x c "$root/tests/compiler/c0/gnu_local_fixed_register_binding_reject.c" -o "$work/reject.i"
set +e
"$minic" -S "$work/reject.i" -o "$work/reject.s" >"$work/reject.stdout" 2>"$work/reject.stderr"
status=$?
set -e
test "$status" -ne 0
grep -F 'local fixed register binding is not supported by this target' "$work/reject.stderr" >/dev/null
printf '%s\n' 'PASS compiler/c0/gnu_local_fixed_register_bindings owner=side-table local-abi=unchanged asm-binding=a0,a1,a7'
