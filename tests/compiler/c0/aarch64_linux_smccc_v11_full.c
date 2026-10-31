/* Linux 6.6 include/linux/arm-smccc.h __arm_smccc_1_1 register ABI:
 * x0..x3 are the four explicit result operands, x0..x7 hold arguments.
 * The caller's C register aliases are GNU operand constraints, not a
 * promise that an arbitrary register is available for the hypercall. */
#define SMCCC_REGS(ARG)                                                    \
    register unsigned long r0 asm("r0");                                  \
    register unsigned long r1 asm("r1");                                  \
    register unsigned long r2 asm("r2");                                  \
    register unsigned long r3 asm("r3");                                  \
    register unsigned long arg0 asm("r0") = (ARG);                         \
    register unsigned long arg1 asm("r1") = (ARG) + 1UL;                   \
    register unsigned long arg2 asm("r2") = (ARG) + 2UL;                   \
    register unsigned long arg3 asm("r3") = (ARG) + 3UL;                   \
    register unsigned long arg4 asm("r4") = (ARG) + 4UL;                   \
    register unsigned long arg5 asm("r5") = (ARG) + 5UL;                   \
    register unsigned long arg6 asm("r6") = (ARG) + 6UL;                   \
    register unsigned long arg7 asm("r7") = (ARG) + 7UL

#define SMCCC_OUTPUTS "=r" (r0), "=r" (r1), "=r" (r2), "=r" (r3)
#define SMCCC_INPUT1 "r" (arg0)
#define SMCCC_INPUT2 SMCCC_INPUT1, "r" (arg1)
#define SMCCC_INPUT4 SMCCC_INPUT2, "r" (arg2), "r" (arg3)
#define SMCCC_INPUT8 SMCCC_INPUT4, "r" (arg4), "r" (arg5), \
                     "r" (arg6), "r" (arg7)
#define SMCCC_SAVE(RESULT)                                               \
    (RESULT)[0] = r0; (RESULT)[1] = r1;                                  \
    (RESULT)[2] = r2; (RESULT)[3] = r3

void minic_smccc_hvc_1(unsigned long argument, unsigned long *result) {
    SMCCC_REGS(argument);
    asm volatile("hvc\t#0\n" : SMCCC_OUTPUTS : SMCCC_INPUT1 : "memory");
    SMCCC_SAVE(result);
}

void minic_smccc_smc_2(unsigned long argument, unsigned long *result) {
    SMCCC_REGS(argument);
    asm volatile("smc\t#0\n" : SMCCC_OUTPUTS : SMCCC_INPUT2 : "memory");
    SMCCC_SAVE(result);
}

void minic_smccc_hvc_4(unsigned long argument, unsigned long *result) {
    SMCCC_REGS(argument);
    asm volatile("hvc\t#0\n" : SMCCC_OUTPUTS : SMCCC_INPUT4 : "memory");
    SMCCC_SAVE(result);
}

void minic_smccc_hvc_8(unsigned long argument, unsigned long *result) {
    SMCCC_REGS(argument);
    asm volatile("hvc\t#0\n" : SMCCC_OUTPUTS : SMCCC_INPUT8 : "memory");
    SMCCC_SAVE(result);
}

void minic_smccc_smc_8(unsigned long argument, unsigned long *result) {
    SMCCC_REGS(argument);
    asm volatile("smc\t#0\n" : SMCCC_OUTPUTS : SMCCC_INPUT8 : "memory");
    SMCCC_SAVE(result);
}

/* The real 6.6 arm64 KVM __arm_smccc_1_1 wrappers bind C pointers and
 * __u64/phys_addr_t (unsigned long long) directly to argument x1..x7.
 * LP64 register transfer preserves their full 64-bit representations. */
void minic_smccc_hvc_pointer(void *guest, unsigned long *result) {
    register unsigned long r0 asm("r0"), r1 asm("r1");
    register unsigned long r2 asm("r2"), r3 asm("r3");
    register unsigned long arg0 asm("r0") = 0xc6000000UL;
    register void *arg1 asm("r1") = guest;
    asm volatile("hvc\t#0\n" : SMCCC_OUTPUTS : SMCCC_INPUT2 : "memory");
    SMCCC_SAVE(result);
}

void minic_smccc_smc_phys(unsigned long long ipa, void *host,
                          unsigned long *result) {
    register unsigned long r0 asm("r0"), r1 asm("r1");
    register unsigned long r2 asm("r2"), r3 asm("r3");
    register unsigned long arg0 asm("r0") = 0x84000000UL;
    register unsigned long long arg1 asm("r1") = ipa;
    register void *arg2 asm("r2") = host;
    register unsigned long arg3 asm("r3") = 0U;
    asm volatile("smc\t#0\n" : SMCCC_OUTPUTS : SMCCC_INPUT4 : "memory");
    SMCCC_SAVE(result);
}

enum minic_hyp_vector { MINIC_VECTOR_DIRECT, MINIC_VECTOR_INDIRECT };
void minic_smccc_hvc_enum(enum minic_hyp_vector slot,
                         unsigned long *result) {
    register unsigned long r0 asm("r0"), r1 asm("r1");
    register unsigned long r2 asm("r2"), r3 asm("r3");
    register unsigned long arg0 asm("r0") = 0xc6000001UL;
    register enum minic_hyp_vector arg1 asm("r1") = slot;
    asm volatile("hvc\t#0\n" : SMCCC_OUTPUTS : SMCCC_INPUT2 : "memory");
    SMCCC_SAVE(result);
}

void minic_smccc_hvc_const_ipa(void *mmu, unsigned long long ipa,
                              unsigned long *result) {
    register unsigned long r0 asm("r0"), r1 asm("r1");
    register unsigned long r2 asm("r2"), r3 asm("r3");
    register unsigned long arg0 asm("r0") = 0xc6000002UL;
    register void *arg1 asm("r1") = mmu;
    register const unsigned long long arg2 asm("r2") = ipa;
    register unsigned int arg3 asm("r3") = 1U;
    asm volatile("hvc\t#0\n" : SMCCC_OUTPUTS : SMCCC_INPUT4 : "memory");
    SMCCC_SAVE(result);
}
