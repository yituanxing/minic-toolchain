/* Original Linux atomic_xchg family: a one-byte _Bool has real byte storage.
 * The typed MachineIR form must preserve the atomic memory RMW, not turn the
 * boolean into a register-only computation. */
int minic_linux_bool_atomic_xchg(_Bool *value)
{
    _Bool prior = 0;
    __asm__ __volatile__("xchgb %b0, %1\n"
        : "+q" (prior), "+m" (*value)
        : : "memory", "cc");
    return (int)prior;
}

#ifdef MINIC_BOOL_XCHG_NEGATIVE
int minic_bad_bool_atomic_xchg(_Bool *value)
{
    _Bool prior = 0;
    /* This version lacks Linux's required compiler memory barrier. */
    __asm__ __volatile__("xchgb %b0, %1\n"
        : "+q" (prior), "+m" (*value)
        : : "cc");
    return (int)prior;
}
#endif
