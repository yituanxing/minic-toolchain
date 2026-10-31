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
# x86 backend now consumes the same Core FIXED_REGISTER_READ instruction
# with an ABI-owned live %rsp value, not an ELF symbol. Native GNU assemble
# and real stack-frame execution are additionally checked by x86-native-m1.
"$minic" --target=x86_64-linux -S "$work/x86-gcc-fixed-register.i" \
    -o "$work/x86-gcc-fixed-register.s"
grep -F '  movq %rsp, %rax' "$work/x86-gcc-fixed-register.s" >/dev/null
if grep -F 'current_stack_pointer@' "$work/x86-gcc-fixed-register.s" >/dev/null; then
    printf '%s\n' 'FAIL compiler/c0/gnu_fixed_register_bindings: x86 rsp treated as external symbol' >&2
    exit 1
fi
cat > "$work/x86-unqualified-register.i" <<'SRC'
register unsigned long unsupported_register asm("r15");
unsigned long read_unqualified_register(void) { return unsupported_register; }
SRC
if "$minic" --target=x86_64-linux -S "$work/x86-unqualified-register.i" \
    -o "$work/x86-unqualified-register.s" \
    >"$work/x86-negative.out" 2>"$work/x86-negative.err"; then
    printf '%s\n' 'FAIL compiler/c0/gnu_fixed_register_bindings: unsupported x86 r15 accepted' >&2
    exit 1
fi
grep -F 'fixed register binding is not supported by this target' \
    "$work/x86-negative.err" >/dev/null
printf '%s\n' 'PASS compiler/c0/gnu_fixed_register_bindings source_asm_aliases=3 RV64=sp,tp x86_rsp=live SSA+machine r15=fail_closed'
