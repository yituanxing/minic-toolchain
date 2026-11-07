/* Exact GNU Linux 6.6.143 arch_change_bit() x86-64 ASM source form.
 * The constant branch must be an atomic LOCK XOR of the selected byte, with
 * a real .smp_locks patch record. It is NOT an OR/AND, barrier, or plain XOR.
 */
static inline __attribute__((__gnu_inline__)) __attribute__((__always_inline__)) void
linux_arch_change_bit(long nr, volatile unsigned long *addr)
{
    if (__builtin_constant_p(nr)) {
        asm volatile(".pushsection .smp_locks,\"a\"\n"
                     ".balign 4\n"
                     ".long 671f - .\n"
                     ".popsection\n"
                     "671:\n\tlock; xorb %b1,%0"
                     : "+m" (*(volatile char *)((void *)addr + (nr >> 3)))
                     : "iq" (1 << (nr & 7)));
    } else {
        asm volatile(".pushsection .smp_locks,\"a\"\n"
                     ".balign 4\n"
                     ".long 671f - .\n"
                     ".popsection\n"
                     "671:\n\tlock; btcq %1,%0"
                     : : "m" (*(volatile long *)addr), "Ir" (nr) : "memory");
    }
}

void x86_p2_change_fixed(volatile unsigned long *addr)
{
    linux_arch_change_bit(9, addr);
}

void x86_p2_change_variable(long nr, volatile unsigned long *addr)
{
    linux_arch_change_bit(nr, addr);
}
