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
grep -F 'read_stack_pointer_plain_asm:' "$work/gnu_fixed_register_bindings.s" >/dev/null
# Core serializes the architectural register read through a temporary
# (mv t0,tp/sp ; sd t0 ; ld a0). The old 'mv a0' oracle was stale.
grep -Eq '^  mv [a-z][a-z0-9]*, tp$' "$work/gnu_fixed_register_bindings.s"
test "$(grep -Ec '^  mv [a-z][a-z0-9]*, sp$' "$work/gnu_fixed_register_bindings.s")" -ge 2
! grep -F 'riscv_current_is_tp' "$work/gnu_fixed_register_bindings.s" >/dev/null
! grep -F 'current_stack_pointer' "$work/gnu_fixed_register_bindings.s" >/dev/null

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

cat > "$work/x86-gcc-fixed-register.i" <<'SRC'
register unsigned long current_stack_pointer asm("rsp");
unsigned long get_current_stack_pointer(void) { return current_stack_pointer; }
SRC
# Parsing GNU asm("rsp") is not machine qualification. Existing x86
# backend does not own this fixed-register binding yet: fail safely.
if "$minic" --target=x86_64-linux -S "$work/x86-gcc-fixed-register.i" \
    -o "$work/x86-gcc-fixed-register.s" \
    >"$work/x86.stdout" 2>"$work/x86.stderr"; then
    printf '%s\n' 'FAIL compiler/c0/gnu_fixed_register_bindings: unqualified x86 rsp binding emitted' >&2
    exit 1
fi
grep -F 'fixed register binding is not supported by this target' "$work/x86.stderr" >/dev/null
printf '%s\n' 'PASS compiler/c0/gnu_fixed_register_bindings source_asm_aliases=3 RV64=sp,tp x86_rsp=fail_closed'
