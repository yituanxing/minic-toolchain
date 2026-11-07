/* The original Linux qspinlock queued_fetch_set_pending_acquire pattern:
 * the SMP patch entry must relocate to LOCK BTSL, not to SETC. */
unsigned int minic_linux_qspinlock_bitcc(unsigned int *word) {
    _Bool carry;
    __asm__ __volatile__(
        ".pushsection .smp_locks,\"a\"\n"
        ".balign 4\n"
        ".long 671f - .\n"
        ".popsection\n"
        "671:\n"
        "\n\tlock; "
#if MINIC_QSPINLOCK_BITCC_NEGATIVE == 4
        "btrl"
#else
        "btsl"
#endif
        " %[val], %[var]"
        "\n\t/* output condition code c */\n"
        : [var] "+m" (*word),
#if MINIC_QSPINLOCK_BITCC_NEGATIVE == 1
          "=@ccz"
#else
          "=@ccc"
#endif
          (carry)
        : [val] "I" (
#if MINIC_QSPINLOCK_BITCC_NEGATIVE == 2
          (0 + 9)
#else
          (0 + 8)
#endif
          )
#if MINIC_QSPINLOCK_BITCC_NEGATIVE != 3
        : "memory"
#endif
    );
    return (unsigned int)carry * (1U << 8);
}
