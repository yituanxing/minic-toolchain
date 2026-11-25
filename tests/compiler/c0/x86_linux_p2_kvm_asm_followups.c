/* GNU-only source oracle for original Linux 6.6.143 arch/x86/kvm/x86.i:
 * div_frac():64365, record_steal_time():65105-65126. No MiniC PASS claim.
 * These are real x86 integer-division, exchange and extable contracts. */
#include <stdint.h>

struct __large_struct {
    unsigned long buf[100];
};

uint32_t x86_p2_kvm_div_frac(uint32_t dividend, uint32_t divisor)
{
    uint32_t quotient, remainder;
    asm("divl %2"
        : "=a" (quotient), "=d" (remainder)
        : "rm" (divisor), "0" (0), "1" ((uint32_t)dividend));
    return quotient;
}

/* In memory-operand XCHG, x86 implicitly performs an atomic exchange even
 * without an explicit LOCK prefix. The early-clobber `+&r` errno must not
 * overlap the byte source or address. The extable is real fault metadata. */
int x86_p2_kvm_clear_steal_preempted(volatile unsigned char *address,
                                     unsigned char *previous)
{
    unsigned char old = 0;
    int err = -14;
    asm volatile("1: xchgb %0, %2\n"
                 "xor %1, %1\n"
                 "2:\n"
                 " .pushsection \"__ex_table\",\"a\"\n"
                 " .balign 4\n"
                 " .long (1b) - .\n"
                 " .long (2b) - .\n"
                 " .long 3\n"
                 " .popsection\n"
                 : "+q" (old), "+&r" (err), "+m" (*address));
    if (err) return err;
    *previous = old;
    return 0;
}

/* Exact exception-fixup asm-goto shape in record_steal_time(): the fault
 * target is operand %l2, not a direct jump. A kernel fixup handler consults
 * the __ex_table relocation; user-space cannot exercise the fault path. */
int x86_p2_kvm_put_user_byte(unsigned char value,
                             volatile unsigned char *address)
{
    asm goto("\n"
             "1:\tmovb %0,%1\n"
             " .pushsection \"__ex_table\",\"a\"\n"
             " .balign 4\n"
             " .long (1b) - .\n"
             " .long (%l2) - .\n"
             " .long 3\n"
             " .popsection\n"
             : : "iq" (value), "m" (*(struct __large_struct *)address)
             : : fault);
    return 0;
fault:
    return -14;
}

int x86_p2_kvm_get_user_byte(volatile unsigned char *address,
                             unsigned char *value)
{
    unsigned char read_value;
    asm volatile goto("\n"
                      "1:\tmovb %[umem],%[output]\n"
                      " .pushsection \"__ex_table\",\"a\"\n"
                      " .balign 4\n"
                      " .long (1b) - .\n"
                      " .long (%l2) - .\n"
                      " .long 3\n"
                      " .popsection\n"
                      : [output] "=q" (read_value)
                      : [umem] "m" (*(struct __large_struct *)address)
                      : : fault);
    *value = read_value;
    return 0;
fault:
    return -14;
}
