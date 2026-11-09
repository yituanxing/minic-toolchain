#!/usr/bin/env bash
# Exercise the exact Linux do_softirq RSP/R11 stack handoff under GNU and
# MiniC.  The alternate stack has a 16-byte aligned top, so the CALL has the
# same SysV entry alignment as the original kernel path.
set -Eeuo pipefail
mini=${1:?MiniC compiler}
test -x "$mini"
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT
cat > "$d/probe.c" <<'SRC'
register unsigned long current_stack_pointer asm("rsp");
static unsigned long softirq_stack[8192] __attribute__((aligned(16)));
static volatile unsigned long signal_value;

__attribute__((noinline)) void __do_softirq(void)
{
    unsigned long local = 19;
    signal_value += local;
}

__attribute__((noinline)) unsigned long probe_softirq(void)
{
    register void *tos asm("r11");
    tos = (void *)&softirq_stack[8190];
    asm __inline volatile(
        "movq %%rsp, (%[tos])\n"
        "movq %[tos], %%rsp\n"
        "call %c[__func]\n"
        "998:\n"
        ".pushsection .discard.reachable\n"
        ".long 998b\n"
        ".popsection\n"
        "popq %%rsp\n"
        : "+r" (tos), "+r" (current_stack_pointer)
        : [__func] "i" (__do_softirq), [tos] "r" (tos)
        : "cc", "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "memory");
    return signal_value;
}

int main(void)
{
    return probe_softirq() == 19 ? 0 : 1;
}
SRC
gcc -O0 -fno-pie -no-pie "$d/probe.c" -o "$d/gnu"
"$d/gnu"
"$mini" --target=x86_64-linux -fno-PIE -S "$d/probe.c" -o "$d/mini.s"
gcc -no-pie -x assembler "$d/mini.s" -o "$d/mini"
"$d/mini"
objdump -dr "$d/mini" | grep -q 'call.*__do_softirq'
echo 'MINIC_X86_SOFTIRQ_STACK_NATIVE=PASS GNU=0 MiniC=0 RSP_restore=1 call=1'
