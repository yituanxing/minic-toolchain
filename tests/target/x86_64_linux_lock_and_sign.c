/* Original Linux arch_clear_bit_unlock_is_negative_byte: the signed result
 * belongs to the same locked byte memory RMW that gets an SMP patch record. */
typedef _Bool bool;

#if defined(MINIC_LOCK_AND_SIGN_NEGATIVE) && MINIC_LOCK_AND_SIGN_NEGATIVE == 1
#define MINIC_LOCK_SIGN_OPCODE "orb %2,%1"
#else
#define MINIC_LOCK_SIGN_OPCODE "andb %2,%1"
#endif

#if defined(MINIC_LOCK_AND_SIGN_NEGATIVE) && MINIC_LOCK_AND_SIGN_NEGATIVE == 2
#define MINIC_LOCK_SIGN_CC "=@ccc"
#else
#define MINIC_LOCK_SIGN_CC "=@ccs"
#endif

#if defined(MINIC_LOCK_AND_SIGN_NEGATIVE) && MINIC_LOCK_AND_SIGN_NEGATIVE == 3
#define MINIC_LOCK_SIGN_CLOBBER "cc"
#else
#define MINIC_LOCK_SIGN_CLOBBER "memory"
#endif

#if defined(MINIC_LOCK_AND_SIGN_NEGATIVE) && MINIC_LOCK_AND_SIGN_NEGATIVE == 4
#define MINIC_LOCK_SIGN_INPUT "r"
#else
#define MINIC_LOCK_SIGN_INPUT "ir"
#endif

#if defined(MINIC_LOCK_AND_SIGN_NEGATIVE) && MINIC_LOCK_AND_SIGN_NEGATIVE == 5
#define MINIC_LOCK_SIGN_COMMENT "\n\t/* output condition code z */\n"
#else
#define MINIC_LOCK_SIGN_COMMENT "\n\t/* output condition code s */\n"
#endif

__attribute__((noinline)) bool minic_lock_and_sign(
    long nr, volatile unsigned long *addr) {
    bool negative;
    asm volatile(".pushsection .smp_locks,\"a\"\n"
                 ".balign 4\n"
                 ".long 671f - .\n"
                 ".popsection\n"
                 "671:\n\tlock; " MINIC_LOCK_SIGN_OPCODE
                 MINIC_LOCK_SIGN_COMMENT
        : MINIC_LOCK_SIGN_CC (negative), "+m" (*(volatile char *)addr)
        : MINIC_LOCK_SIGN_INPUT ((char)~(1 << nr))
        : MINIC_LOCK_SIGN_CLOBBER);
    return negative;
}
