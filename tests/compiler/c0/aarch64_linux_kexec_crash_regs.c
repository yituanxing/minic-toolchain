/* Linux 6.6 arch/arm64/include/asm/kexec.h crash_setup_regs(), verbatim
 * inline-assembly contract with two early-clobber register outputs and a
 * pointer input. The stored register/PSTATE/PC image is a hardware effect. */
typedef unsigned long long u64;
struct arm64_crash_regs {
    u64 regs[31];
    u64 sp, pc, pstate;
};

void arm64_crash_setup_regs(struct arm64_crash_regs *newregs) {
    u64 tmp1, tmp2;
    __asm__ __volatile__ (
        "stp\t x0,   x1, [%2, #16 *  0]\n"
        "stp\t x2,   x3, [%2, #16 *  1]\n"
        "stp\t x4,   x5, [%2, #16 *  2]\n"
        "stp\t x6,   x7, [%2, #16 *  3]\n"
        "stp\t x8,   x9, [%2, #16 *  4]\n"
        "stp\tx10,  x11, [%2, #16 *  5]\n"
        "stp\tx12,  x13, [%2, #16 *  6]\n"
        "stp\tx14,  x15, [%2, #16 *  7]\n"
        "stp\tx16,  x17, [%2, #16 *  8]\n"
        "stp\tx18,  x19, [%2, #16 *  9]\n"
        "stp\tx20,  x21, [%2, #16 * 10]\n"
        "stp\tx22,  x23, [%2, #16 * 11]\n"
        "stp\tx24,  x25, [%2, #16 * 12]\n"
        "stp\tx26,  x27, [%2, #16 * 13]\n"
        "stp\tx28,  x29, [%2, #16 * 14]\n"
        "mov\t %0,  sp\n"
        "stp\tx30,  %0,  [%2, #16 * 15]\n"
        "/* faked current PSTATE */\n"
        "mrs\t %0, CurrentEL\n"
        "mrs\t %1, SPSEL\n"
        "orr\t %0, %0, %1\n"
        "mrs\t %1, DAIF\n"
        "orr\t %0, %0, %1\n"
        "mrs\t %1, NZCV\n"
        "orr\t %0, %0, %1\n"
        "adr\t %1, 1f\n"
        "1:\n"
        "stp\t %1, %0,   [%2, #16 * 16]\n"
        : "=&r" (tmp1), "=&r" (tmp2)
        : "r" (newregs)
        : "memory"
    );
}
