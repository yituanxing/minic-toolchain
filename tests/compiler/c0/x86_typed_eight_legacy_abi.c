/* Eight independent x86 Linux ASM source families, including the immutable
 * GNU ELF records in .tracedata and .altinstructions. Privileged/special ISA
 * functions exist for GNU AS, but only user-safe functions are executed. */
typedef unsigned long u64;
typedef unsigned long size_t;
typedef unsigned int u32;
typedef _Bool bool;

void checked_pm_trace(u64 *a, u64 *b) {
    void *pa, *pb;
    asm volatile(" movq $1f,%0\n"
        ".section .tracedata,\"a\"\n"
        "1:\t.word %c1\n\t"
        " .quad %c2\n"
        ".previous"
        : "=r"(pa) : "i"(598), "i"("drivers/base/power/main.c"));
    asm volatile(" movq $1f,%0\n"
        ".section .tracedata,\"a\"\n"
        "1:\t.word %c1\n\t"
        " .quad %c2\n"
        ".previous"
        : "=r"(pb) : "i"(1629), "i"("drivers/base/power/main.c"));
    *a=(u64)pa;
    *b=(u64)pb;
}
void checked_movntdqa(void *dst, const void *src) {
    asm("movntdqa (%0), %%xmm0\nmovups %%xmm0, (%1)\n"
        :: "r"(src), "r"(dst) : "memory");
}
unsigned long checked_pvclock(unsigned long now, unsigned long fraction) {
    unsigned long lo, hi;
    asm("mulq %[mul_frac]; shrd $32, %[hi], %[lo]"
        : [lo] "=a"(lo), [hi] "=d"(hi)
        : "0"(now), [mul_frac] "rm"(fraction));
    return lo;
}
register unsigned long current_stack_pointer asm("rsp");
unsigned int preempt_counter;
void __SCT__preempt_schedule(void) { preempt_counter += 3U; }
void __SCT__preempt_schedule_notrace(void) { preempt_counter += 5U; }
void checked_preempt(void) {
    asm volatile("call __SCT__preempt_schedule" : "+r"(current_stack_pointer));
}
void checked_preempt_notrace(void) {
    asm volatile("call __SCT__preempt_schedule_notrace" : "+r"(current_stack_pointer));
}
u64 checked_ipv6(const u64 *src, const u64 *dst, u64 input) {
    u64 result;
    asm("addq (%[saddr]),%[sum]\n\t"
        "adcq 8(%[saddr]),%[sum]\n\t"
        "adcq (%[daddr]),%[sum]\n\t"
        "adcq 8(%[daddr]),%[sum]\n\t"
        "adcq $0,%[sum]\n"
        : [sum] "=r"(result)
        : "[sum]"(input), [saddr] "r"(src), [daddr] "r"(dst));
    return result;
}
int checked_boot_compare(const void *a, const void *b, size_t length) {
    bool diff;
    asm("repe; cmpsb\n\t/* output condition code nz */\n"
        : "=@ccnz"(diff), "+D"(a), "+S"(b), "+c"(length));
    return diff;
}
struct OldTv { long sec; long usec; };
struct Timezone { int minutes; int dst; };
long checked_vdso_syscall(struct OldTv *tv, struct Timezone *tz) {
    long ret;
    asm("syscall" : "=a"(ret)
        : "0"(96), "D"(tv), "S"(tz) : "memory");
    return ret;
}
unsigned int checked_cpunode(void) {
    unsigned int p;
    asm volatile(
        "# ALT: oldnstr\n"
        "661:\n\tlsl %[seg],%[p]\n"
        "662:\n# ALT: padding\n"
        ".skip -(((6651f-6641f)-(662b-661b)) > 0) * "
        "((6651f-6641f)-(662b-661b)),0x90\n"
        "663:\n"
        ".pushsection .altinstructions,\"a\"\n"
        " .long 661b - .\n .long 6641f - .\n"
        " .4byte (16*32+22)\n"
        " .byte 663b-661b\n .byte 6651f-6641f\n"
        ".popsection\n"
        ".pushsection .altinstr_replacement, \"ax\"\n"
        "# ALT: replacement 1\n"
        "6641:\n\t.byte 0xf3,0x0f,0xc7,0xf8\n"
        "6651:\n.popsection\n"
        : [p] "=a"(p) : "i"(0), [seg] "r"(15*8+3));
    return p;
}
int main(void) {
    u64 src[2] = {0, 0}, dst[2] = {0, 0};
    unsigned char a[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    unsigned char b[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    struct OldTv tv = {0};
    struct Timezone tz = {0};
    if (checked_ipv6(src, dst, 42) != 42) return 1;
    if (checked_boot_compare(a, b, 8) != 0) return 2;
    if (checked_pvclock(0x100000000UL, 1UL) != 1UL) return 3;
    checked_preempt();
    checked_preempt_notrace();
    if (preempt_counter != 8U) return 4;
    if (checked_vdso_syscall(&tv, &tz) != 0) return 5;
    if (tv.sec <= 0) return 6;
    return 42;
}
