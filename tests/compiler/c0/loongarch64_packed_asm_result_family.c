/* GNU C output lvalues may reside in packed storage. The ASM opcode itself
 * stays target-owned; only the compiler-produced result writeback must use
 * the typed/alignment-aware memory transport. */
struct __attribute__((packed)) PackedResults {
    unsigned char pad;
    unsigned int word;
    int clock_tid;
    unsigned long wide;
};

void packed_clock(struct PackedResults *p) {
    __asm__ __volatile__("rdtime.d %0, %1"
        : "=r"(p->wide), "=r"(p->clock_tid));
}

void packed_cpucfg(struct PackedResults *p, unsigned int index) {
    __asm__ __volatile__("cpucfg %0, %1\n\t"
        : "=&r"(p->word) : "r"(index));
}

void packed_nospec(struct PackedResults *p, unsigned long i,
                   unsigned long n) {
    __asm__ __volatile__(
        "sltu\t%0, %1, %2\n\tsub.d\t%0, $zero, %0\n\t"
        : "=r"(p->wide) : "r"(i), "r"(n) :);
}

void packed_percpu_amo(struct PackedResults *p, unsigned long long *mem,
                       unsigned long value) {
    __asm__ __volatile__("amadd.d %[ret], %[val], %[ptr]"
        : [ret] "=&r"(p->wide), [ptr] "+ZB"(*mem)
        : [val] "r"(value));
}

void packed_frametrace_csr(struct PackedResults *p) {
    __asm__ __volatile__("csrrd %0, 0x7" : "=r"(p->wide));
}

/* A static scalar has a proven 8-byte global alignment and does not need
 * the runtime guard; retain this fast path when refactoring the helper. */
static unsigned long native_global_result;
void aligned_global_frametrace_csr(void) {
    __asm__ __volatile__("csrrd %0, 0x7" : "=r"(native_global_result));
}
