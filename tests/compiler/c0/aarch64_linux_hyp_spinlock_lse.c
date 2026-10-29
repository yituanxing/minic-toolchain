/* Linux 6.6.143 arch/arm64/kvm/hyp/include/nvhe/spinlock.h, with only
 * its kernel macro wrappers made self-contained for host preprocessing.
 * All GNU instructions, operands, labels, ALTERNATIVE and PREL32 remain
 * source-faithful. GNU AS must see exactly 5 LL/SC vs 5 LSE instructions
 * in the lock and 3 vs 3 in the unlock. */
typedef unsigned int u32;
typedef unsigned long u64;
typedef unsigned short u16;
typedef union hyp_spinlock {
    u32 __val;
    struct { u16 owner, next; };
} hyp_spinlock_t;

#define __LSE_PREAMBLE ".arch_extension lse\n"
#define __nops(n) ".rept\t" #n "\nnop\n.endr\n"
#define __ALTERNATIVE_CFG(oldinstr, newinstr) \
    ".if 1 == 1\n" \
    "661:\n\t" oldinstr "\n" \
    "662:\n" \
    ".pushsection .altinstructions,\"a\"\n" \
    ".word 661b - .\n" \
    ".word 663f - .\n" \
    ".hword 33\n" \
    ".byte 662b - 661b\n" \
    ".byte 664f - 663f\n" \
    ".popsection\n.subsection 1\n" \
    "663:\n\t" newinstr "\n" \
    "664:\n\t" \
    ".org . - (664b-663b) + (662b-661b)\n\t" \
    ".org . - (662b-661b) + (664b-663b)\n\t" \
    ".previous\n.endif\n"
#define ARM64_LSE_ATOMIC_INSN(llsc, lse) \
    __ALTERNATIVE_CFG(llsc, __LSE_PREAMBLE lse)

static inline void hyp_spin_lock(hyp_spinlock_t *lock) {
    u32 tmp;
    hyp_spinlock_t lockval, newval;
    asm volatile(
        ARM64_LSE_ATOMIC_INSN(
            "\tprfm\tpstl1strm, %3\n"
            "1:\tldaxr\t%w0, %3\n"
            "\tadd\t%w1, %w0, #(1 << 16)\n"
            "\tstxr\t%w2, %w1, %3\n"
            "\tcbnz\t%w2, 1b\n",
            "\tmov\t%w2, #(1 << 16)\n"
            "\tldadda\t%w2, %w0, %3\n"
            __nops(3))
        "\teor\t%w1, %w0, %w0, ror #16\n"
        "\tcbz\t%w1, 3f\n"
        "\tsevl\n"
        "2:\twfe\n"
        "\tldaxrh\t%w2, %4\n"
        "\teor\t%w1, %w2, %w0, lsr #16\n"
        "\tcbnz\t%w1, 2b\n"
        "3:"
        : "=&r"(lockval), "=&r"(newval), "=&r"(tmp), "+Q"(*lock)
        : "Q"(lock->owner)
        : "memory");
}

static inline void hyp_spin_unlock(hyp_spinlock_t *lock) {
    u64 tmp;
    asm volatile(
        ARM64_LSE_ATOMIC_INSN(
            "\tldrh\t%w1, %0\n"
            "\tadd\t%w1, %w1, #1\n"
            "\tstlrh\t%w1, %0",
            "\tmov\t%w1, #1\n"
            "\tstaddlh\t%w1, %0\n"
            __nops(1))
        : "=Q"(lock->owner), "=&r"(tmp)
        : : "memory");
}

void hyp_spin_roundtrip(hyp_spinlock_t *lock) {
    hyp_spin_lock(lock);
    hyp_spin_unlock(lock);
}
