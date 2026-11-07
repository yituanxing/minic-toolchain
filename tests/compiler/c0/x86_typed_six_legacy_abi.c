/* R5: six Source -> Core -> typed Machine ASM families, plus the original
 * overlapping eight-operand CPUID that MUST remain on the legacy verifier.
 * Privileged instructions and TPAUSE are GNU-AS-only, never run in user mode. */
typedef unsigned int u32;
static u32 flags;
static volatile unsigned long cache_line;
static unsigned short selector;

void checked_verw(void) {
    asm volatile("verw %[ds]" :: [ds] "m"(selector) : "cc");
}
void privileged_invlpg(unsigned long address) {
    asm volatile("invlpg (%0)" :: "r"(address) : "memory");
}
void checked_clflush(volatile unsigned long *address) {
    asm volatile("clflush %0" : "+m"(*address));
}
unsigned long checked_delay_mul(unsigned long loops, unsigned long multiplier) {
    int scratch;
    asm("mull %%edx" : "=d"(loops), "=&a"(scratch)
        : "1"(loops), "0"(multiplier));
    return loops;
}
unsigned long checked_delay_wait(unsigned long loops) {
    asm volatile("test %0,%0\n\tjz 3f\n\tjmp 1f\n\t"
                 ".align 16\n1:\tjmp 2f\n\t.align 16\n"
                 "2:\tdec %0\n\tjnz 2b\n3:\tdec %0\n"
        : "+a"(loops));
    return loops;
}
void checked_tpause(u32 ecx, u32 edx, u32 eax) {
    asm volatile("tpause %%ecx\n" :: "c"(ecx), "d"(edx), "a"(eax));
}
void privileged_halt(void) {
    asm volatile("hlt");
}
void checked_movnti(unsigned long value) {
    asm("movntiq %1, %0" : "=m"(cache_line) : "r"(value));
}
void checked_set_bit(u32 n) {
    asm volatile("btsl %1,%0" : "+m"(flags) : "Ir"(n));
}
void checked_cpuid_six(u32 id, u32 count, u32 *a, u32 *b, u32 *c, u32 *d) {
    asm volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
        : "0"(id), "2"(count));
}
void checked_cpuid_eight(u32 *a, u32 *b, u32 *c, u32 *d) {
    /* Historically accepted by generic qualified() with 8 Core bindings.
     * BOOT_FLAGS typed ID must never intercept this valid older shape. */
    asm("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
        : "0"(*a), "1"(*b), "2"(*c), "3"(*d));
}

int main(void) {
    u32 first[4] = {0, 0, 0, 0};
    u32 second[4] = {0, 0, 0, 0};
    unsigned int i;
    checked_cpuid_six(0, 0, &first[0], &first[1], &first[2], &first[3]);
    checked_cpuid_eight(&second[0], &second[1], &second[2], &second[3]);
    for (i = 0; i < 4; ++i)
        if (first[i] != second[i]) return 1;
    checked_set_bit(3);
    if (flags != 8) return 2;
    checked_movnti(42);
    asm volatile("mfence" ::: "memory");
    if (cache_line != 42) return 3;
    checked_clflush(&cache_line);
    if (checked_delay_mul(0xffffffffUL, 0xffffffffUL) !=
        0xfffffffeUL) return 4;
    return 42;
}
