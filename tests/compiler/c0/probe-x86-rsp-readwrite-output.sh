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
# Original GNU Linux x86_64 alternatives/iret uses live "+r"(rsp) and
# ABI volatile caller-scratch clobbers (rax/rcx). Check all SysV caller
# clobbers in one fixture, not one error per follow-up commit. The source
# must be valid GCC GNU C, but unknown machine ASM must stay rejected by
# MiniC's target-owner qualification, even after the grammar barrier moves.
cat > "$work/original-clobbers.i" <<'EOF'
register unsigned long current_stack_pointer asm("rsp");
void x86_linux_caller_clobbers(void) {
    __asm__ __volatile__("nop" : "+r"(current_stack_pointer) :
        : "cc", "rax", "rcx", "rdx", "rsi", "rdi",
          "r8", "r9", "r10", "r11", "memory");
}
EOF
cc -c -std=gnu11 -x c "$work/original-clobbers.i" \
    -o "$work/reference-original-clobbers.o"
if "$cc" --target=x86_64-linux -S "$work/original-clobbers.i" \
    -o "$work/original-clobbers.s" >"$work/original-clobbers.log" 2>&1; then
    echo 'FAIL: x86_64 arbitrary machine ASM accepted via source clobbers' >&2
    exit 1
fi
if grep -E 'GNU asm output operand requires an lvalue|unsupported GNU asm register clobber for target' \
    "$work/original-clobbers.log" >/dev/null; then
    echo 'FAIL: x86 Linux ABI caller-clobber source grammar still rejected' >&2
    exit 1
fi
cat > "$work/saved-clobbers.i" <<'EOF'
void reject_unsafe_saved(void) {
    __asm__ volatile("nop" ::: "rbx", "r12");
}
EOF
if "$cc" --target=x86_64-linux -S "$work/saved-clobbers.i" \
    -o "$work/saved-clobbers.s" >"$work/saved-clobbers.log" 2>&1; then
    echo 'FAIL: unqualified SysV callee-saved register clobbers admitted' >&2
    exit 1
fi
grep -F 'unsupported GNU asm register clobber for target' \
    "$work/saved-clobbers.log" >/dev/null
echo 'X86_GNU_CALLER_CLOBBERS=PASS original_GCC=1 caller_names=10 saved_rejected=1 machine_fail_closed=1'
echo 'X86_GNU_RSP_TIED_OUTPUT=PASS source_grammar=1 machine_rejected=1 direct_C_write_rejected=1 output_writeonly_rejected=1 cross_target_rejected=1'
