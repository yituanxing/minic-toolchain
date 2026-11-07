/* The complete original Linux 6.6.143 futex GNU exception-table macro.
 * The type-17 handler changes its designated physical output register on
 * EFAULT, while the ordinary success path uses real atomic x86 operations. */
#define FUTEX_SMP_LOCK \
    ".pushsection .smp_locks,\"a\"\n" \
    ".balign 4\n" \
    ".long 671f - .\n" \
    ".popsection\n" \
    "671:\n\tlock; "

#define FUTEX_EX_TABLE(source, target, output) \
    " .pushsection \"__ex_table\",\"a\"\n" \
    " .balign 4\n" \
    " .long (" source ") - .\n" \
    " .long (" target ") - .\n" \
    ".macro extable_type_reg type:req reg:req\n" \
    ".set .Lfound, 0\n" \
    ".set .Lregnr, 0\n" \
    ".irp rs,rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi,r8,r9,r10,r11,r12,r13,r14,r15\n" \
    ".ifc \\reg, %%\\rs\n" \
    ".set .Lfound, .Lfound+1\n" \
    ".long \\type + (.Lregnr << 8)\n" \
    ".endif\n" \
    ".set .Lregnr, .Lregnr+1\n" \
    ".endr\n" \
    ".set .Lregnr, 0\n" \
    ".irp rs,eax,ecx,edx,ebx,esp,ebp,esi,edi,r8d,r9d,r10d,r11d,r12d,r13d,r14d,r15d\n" \
    ".ifc \\reg, %%\\rs\n" \
    ".set .Lfound, .Lfound+1\n" \
    ".long \\type + (.Lregnr << 8)\n" \
    ".endif\n" \
    ".set .Lregnr, .Lregnr+1\n" \
    ".endr\n" \
    ".if (.Lfound != 1)\n" \
    ".error \"extable_type_reg: bad register argument\"\n" \
    ".endif\n" \
    ".endm\n" \
    "extable_type_reg reg=" output ", type=(17 | ((-14) << 16)) \n" \
    ".purgem extable_type_reg\n" \
    " .popsection\n"

static int futex_cmpxchg(unsigned int *uaddr, unsigned int oldval,
                         unsigned int newval, unsigned int *previous)
{
    int ret = 0;
    unsigned int result;
    asm volatile("\n1:\t" FUTEX_SMP_LOCK "cmpxchgl %3, %2\n"
                 "2:\n" FUTEX_EX_TABLE("1b", "2b", "%0")
        : "+r" (ret), "=a" (result), "+m" (*uaddr)
        : "r" (newval), "1" (oldval) : "memory");
    *previous = result;
    return ret;
}

static int futex_xchg(unsigned int *uaddr, int next, int *previous)
{
    int oldval = 0, ret;
    asm volatile("1:\t" "xchgl %0, %2\n" "2:\n"
                 FUTEX_EX_TABLE("1b", "2b", "%1")
        : "=r" (oldval), "=r" (ret), "+m" (*uaddr)
        : "0" (next), "1" (0));
    *previous = oldval;
    return ret;
}

static int futex_xadd(unsigned int *uaddr, int next, int *previous)
{
    int oldval = 0, ret;
    asm volatile("1:\t" FUTEX_SMP_LOCK "xaddl %0, %2\n"
                 "2:\n" FUTEX_EX_TABLE("1b", "2b", "%1")
        : "=r" (oldval), "=r" (ret), "+m" (*uaddr)
        : "0" (next), "1" (0));
    *previous = oldval;
    return ret;
}

static int futex_or(unsigned int *uaddr, int mask, int *previous)
{
    int oldval = 0, ret, temporary;
    asm volatile("1:\tmovl %2, %0\n"
                 "2:\tmovl %0, %3\n"
                 "\torl %4, %3\n"
                 "3:\t" FUTEX_SMP_LOCK "cmpxchgl %3, %2\n"
                 "\tjnz\t2b\n4:\n"
                 FUTEX_EX_TABLE("1b", "4b", "%1")
                 FUTEX_EX_TABLE("3b", "4b", "%1")
        : "=&a" (oldval), "=&r" (ret), "+m" (*uaddr), "=&r" (temporary)
        : "r" (mask), "1" (0));
    *previous = oldval;
    return ret;
}

static int futex_and(unsigned int *uaddr, int mask, int *previous)
{
    int oldval = 0, ret, temporary;
    asm volatile("1:\tmovl %2, %0\n"
                 "2:\tmovl %0, %3\n"
                 "\tandl %4, %3\n"
                 "3:\t" FUTEX_SMP_LOCK "cmpxchgl %3, %2\n"
                 "\tjnz\t2b\n4:\n"
                 FUTEX_EX_TABLE("1b", "4b", "%1")
                 FUTEX_EX_TABLE("3b", "4b", "%1")
        : "=&a" (oldval), "=&r" (ret), "+m" (*uaddr), "=&r" (temporary)
        : "r" (mask), "1" (0));
    *previous = oldval;
    return ret;
}

static int futex_xor(unsigned int *uaddr, int mask, int *previous)
{
    int oldval = 0, ret, temporary;
    asm volatile("1:\tmovl %2, %0\n"
                 "2:\tmovl %0, %3\n"
                 "\txorl %4, %3\n"
                 "3:\t" FUTEX_SMP_LOCK "cmpxchgl %3, %2\n"
                 "\tjnz\t2b\n4:\n"
                 FUTEX_EX_TABLE("1b", "4b", "%1")
                 FUTEX_EX_TABLE("3b", "4b", "%1")
        : "=&a" (oldval), "=&r" (ret), "+m" (*uaddr), "=&r" (temporary)
        : "r" (mask), "1" (0));
    *previous = oldval;
    return ret;
}

int main(void)
{
    unsigned int value = 17, previous = 0;
    int old = 0;
    if (futex_cmpxchg(&value, 17, 42, &previous) ||
        previous != 17 || value != 42) return 1;
    if (futex_cmpxchg(&value, 17, 99, &previous) ||
        previous != 42 || value != 42) return 2;
    if (futex_xchg(&value, 11, &old) || old != 42 || value != 11) return 3;
    if (futex_xadd(&value, 5, &old) || old != 11 || value != 16) return 4;
    if (futex_or(&value, 64, &old) || old != 16 || value != 80) return 5;
    if (futex_and(&value, 64, &old) || old != 80 || value != 64) return 6;
    if (futex_xor(&value, 106, &old) || old != 64 || value != 42) return 7;
    return 42;
}
