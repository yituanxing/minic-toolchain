/* Valid GNU C with target-qualified LoongArch source-owned atomics/CSR.
 * Only compiler-generated outputs to packed C lvalues require typed memory. */
struct __attribute__((packed)) PackedHardwareResult {
    unsigned char pad;
    unsigned long old;
    long signed_old;
};

/* The atomic memory operands themselves are naturally aligned globals. */
static unsigned long naturally_aligned_mem;
static long naturally_aligned_signed_mem;

void packed_pte_result(struct PackedHardwareResult *p, unsigned long flag) {
    __asm__ __volatile__(
        "1: ll.d %1, %0\n"
        "bnez %1, 2f\n"
        "or %1, %1, %2\n"
        "sc.d %1, %0\n"
        "beqz %1, 1b\n"
        "nop\n"
        "2: dbar 0x700\n"
        : "+m"(naturally_aligned_mem), "=&r"(p->old)
        : "r"(flag));
}

void packed_amo_fetch(struct PackedHardwareResult *p, long value) {
    __asm__ __volatile__("amadd_db.d %1, %2, %0"
        : "+ZB"(naturally_aligned_signed_mem), "=&r"(p->signed_old)
        : "r"(value) : "memory");
}

void packed_xchg(struct PackedHardwareResult *p, unsigned long value) {
    __asm__ __volatile__("amswap_db.d %1, %z2, %0"
        : "+ZB"(naturally_aligned_mem), "=&r"(p->old)
        : "Jr"(value) : "memory");
}

void packed_csr_xchg(struct PackedHardwareResult *p, unsigned int mask) {
    __asm__ __volatile__("csrxchg %0, %1, %2"
        : "+r"(p->old) : "r"(mask), "i"(0) : "memory");
}
