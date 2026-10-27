#!/usr/bin/env bash
# C lvalue mutability, GNU output source grammar and actual machine ASM
# are three distinct permissions. Test this separation with x86 Linux rsp.
set -Eeuo pipefail
cc=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cat >"$work/rsp.i" <<'EOF'
register unsigned long current_stack_pointer asm("rsp");
void real_linux_rsp_operand(void) {
    asm volatile("nop" : "+r"(current_stack_pointer));
}
EOF
if "$cc" --target=x86_64-linux -S "$work/rsp.i" \
    -o "$work/rsp.s" >"$work/rsp.log" 2>&1; then
    echo 'FAIL: arbitrary RSP machine template admitted' >&2
    exit 1
fi
if grep -F "GNU asm output operand requires an lvalue" "$work/rsp.log" >/dev/null; then
    echo 'FAIL: valid original GNU x86 +r(rsp) syntax still rejected' >&2
    exit 1
fi
test ! -e "$work/rsp.s"
cat >"$work/direct-write.i" <<'EOF'
register unsigned long current_stack_pointer asm("rsp");
void illegal_c_write(void) { current_stack_pointer = 16UL; }
EOF
if "$cc" --target=x86_64-linux -S "$work/direct-write.i" \
    -o "$work/direct-write.s" >/dev/null 2>&1; then
    echo 'FAIL: arbitrary writable C RSP admitted' >&2
    exit 1
fi
cat >"$work/writeonly.i" <<'EOF'
register unsigned long current_stack_pointer asm("rsp");
void illegal_writeonly(void) { asm volatile("nop" : "=r"(current_stack_pointer)); }
EOF
if "$cc" --target=x86_64-linux -S "$work/writeonly.i" \
    -o "$work/writeonly.s" >/dev/null 2>&1; then
    echo 'FAIL: write-only RSP output admitted' >&2
    exit 1
fi
if "$cc" --target=aarch64-linux -S "$work/rsp.i" \
    -o "$work/not-x86.s" >/dev/null 2>&1; then
    echo 'FAIL: RSP readwrite spelling permitted on AArch64' >&2
    exit 1
fi
echo 'X86_GNU_RSP_TIED_OUTPUT=PASS source_grammar=1 machine_rejected=1 direct_C_write_rejected=1 output_writeonly_rejected=1 cross_target_rejected=1'
