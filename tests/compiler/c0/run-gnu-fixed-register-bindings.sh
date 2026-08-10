#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
minic=${MINIC:-"$root/build/debug/bin/minic"}
host_cc=${HOST_CC:-${CC:-cc}}
work=${BUILD_DIR:-"$root/build/debug"}/tests/compiler-c0-gnu-fixed-register-bindings

rm -rf "$work"
mkdir -p "$work"

"$host_cc" -E -P -x c "$root/tests/compiler/c0/gnu_fixed_register_bindings.c" \
    -o "$work/gnu_fixed_register_bindings.i"
"$minic" -S "$work/gnu_fixed_register_bindings.i" \
    -o "$work/gnu_fixed_register_bindings.s"

test -s "$work/gnu_fixed_register_bindings.s"
grep -F 'read_current_like:' "$work/gnu_fixed_register_bindings.s" >/dev/null
grep -F 'read_stack_pointer_like:' "$work/gnu_fixed_register_bindings.s" >/dev/null
grep -F '  mv a0, tp' "$work/gnu_fixed_register_bindings.s" >/dev/null
grep -F '  mv a0, sp' "$work/gnu_fixed_register_bindings.s" >/dev/null
! grep -F 'la a0, riscv_current_is_tp' "$work/gnu_fixed_register_bindings.s" >/dev/null
! grep -F 'la a0, current_stack_pointer' "$work/gnu_fixed_register_bindings.s" >/dev/null

"$host_cc" -E -P -x c "$root/tests/compiler/c0/gnu_fixed_register_binding_reject.c" \
    -o "$work/gnu_fixed_register_binding_reject.i"
set +e
"$minic" -S "$work/gnu_fixed_register_binding_reject.i" \
    -o "$work/gnu_fixed_register_binding_reject.s" \
    >"$work/reject.stdout" 2>"$work/reject.stderr"
status=$?
set -e
test "$status" -ne 0
grep -F 'fixed register binding is not supported by this target' "$work/reject.stderr" >/dev/null

printf '%s\n' 'PASS compiler/c0/gnu_fixed_register_bindings storage=fixed-register-binding target=RV64 names=tp,sp read=direct-register memory-symbol=none unsupported=s1-reject'
