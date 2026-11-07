/* Linux 6.6.143 arch/loongarch/kernel/traps.c:prepare_frametrace.
 * The complete GNU form is kept intact to exercise the target descriptor. */
struct pt_regs {
    unsigned long regs[32];
    unsigned long csr_era;
};

void loong_prepare_frametrace(struct pt_regs *regs) {
    __asm__ __volatile__(
        "st.d $r1, %1, 8*1\n\t"
        "pcaddi $ra, 0\n\t"
        "st.d $ra, %0\n\t"
        "ld.d $ra, %1, 8\n\t"
        "st.d $r2, %1, 8*2\n\t"
        "st.d $r3, %1, 8*3\n\t"
        "st.d $r4, %1, 8*4\n\t"
        "st.d $r5, %1, 8*5\n\t"
        "st.d $r6, %1, 8*6\n\t"
        "st.d $r7, %1, 8*7\n\t"
        "st.d $r8, %1, 8*8\n\t"
        "st.d $r9, %1, 8*9\n\t"
        "st.d $r10, %1, 8*10\n\t"
        "st.d $r11, %1, 8*11\n\t"
        "st.d $r12, %1, 8*12\n\t"
        "st.d $r13, %1, 8*13\n\t"
        "st.d $r14, %1, 8*14\n\t"
        "st.d $r15, %1, 8*15\n\t"
        "st.d $r16, %1, 8*16\n\t"
        "st.d $r17, %1, 8*17\n\t"
        "st.d $r18, %1, 8*18\n\t"
        "st.d $r19, %1, 8*19\n\t"
        "st.d $r20, %1, 8*20\n\t"
        "st.d $r21, %1, 8*21\n\t"
        "st.d $r22, %1, 8*22\n\t"
        "st.d $r23, %1, 8*23\n\t"
        "st.d $r24, %1, 8*24\n\t"
        "st.d $r25, %1, 8*25\n\t"
        "st.d $r26, %1, 8*26\n\t"
        "st.d $r27, %1, 8*27\n\t"
        "st.d $r28, %1, 8*28\n\t"
        "st.d $r29, %1, 8*29\n\t"
        "st.d $r30, %1, 8*30\n\t"
        "st.d $r31, %1, 8*31\n\t"
        : "=m" (regs->csr_era)
        : "r" (regs->regs)
        : "memory");
}
