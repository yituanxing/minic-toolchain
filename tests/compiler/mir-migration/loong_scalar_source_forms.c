/* Sealed LoongArch Linux scalar ASM: GNU C ingress through typed MachineIR.
 * Privileged CACOP/INVTLB/CSR operations are compile-only, never run in
 * QEMU-user. Keep these source forms identical to the original positive
 * target-qualified contracts rather than inventing synthetic Core input. */

void loong_scalar_cacop(unsigned long address) {
    __asm__ __volatile__("\tcacop %0, %1\n" : :
        "i"(8), "ZC"(*(unsigned char *)(address)));
}

unsigned int loong_scalar_cpucfg(unsigned int index) {
    unsigned int result;
    __asm__ __volatile__("cpucfg %0, %1\n\t"
        : "=&r"(result) : "r"(index));
    return result;
}

unsigned long loong_scalar_fpr_read(void) {
    unsigned long payload;
    __asm__ __volatile__("movfr2gr.d %0, $f0" : "=r"(payload));
    return payload;
}

void loong_scalar_fpr_write(unsigned long payload) {
    __asm__ __volatile__("movgr2fr.d $f0, %0" : : "r"(payload));
}

unsigned long loong_scalar_clock(void) {
    unsigned long value;
    int cpu;
    __asm__ __volatile__("rdtime.d %0, %1"
        : "=r"(value), "=r"(cpu));
    return value + (unsigned long)cpu;
}

/* The remaining four sealed CLOCK variants have intentionally distinct
 * volatility, GNU operand rank and named-early-clobber contracts. */
unsigned long loong_scalar_clock_named(void) {
    unsigned long named_value, named_tid;
    __asm__ __volatile__("rdtime.d %[val], %[tid]"
        : [val] "=&r"(named_value), [tid] "=&r"(named_tid));
    return named_value + named_tid;
}

unsigned long loong_scalar_clock_vdso_base(void) {
    unsigned long vdso_base;
    __asm__("la.pcrel %0, _start" : "=r"(vdso_base));
    return vdso_base;
}

/* The source decoder advertises CLOCK variant 4, but historical source
 * admission rejects it: CPU_ID demands an explicit memory clobber while
 * simultaneously requiring clobber_count == 0. Keep this genuine GNU C
 * form as an intentional negative until source compatibility is widened. */
#ifdef MINIC_LOONG_CLOCK_CPU_ID_HISTORICAL_NEGATIVE
int loong_scalar_clock_cpu_id(void) {
    int cpu_id;
    __asm__ __volatile__("rdtime.d $zero, %0"
        : "=r"(cpu_id) : : "memory");
    return cpu_id;
}
#endif

unsigned long loong_scalar_clock_counter_only(void) {
    unsigned long counter_only;
    __asm__ __volatile__("rdtime.d %0, $zero"
        : "=r"(counter_only));
    return counter_only;
}

/* All four original Linux INVTLB operand layouts. These are privileged. */
void loong_scalar_invtlb(unsigned int asid, unsigned long address) {
    __asm__ __volatile__("invtlb %0, %1, %2" : :
        "i"(0), "r"(asid), "r"(address) : "memory");
    __asm__ __volatile__("invtlb %0, $zero, %1" : :
        "i"(0), "r"(address) : "memory");
    __asm__ __volatile__("invtlb %0, %1, $zero" : :
        "i"(0), "r"(asid) : "memory");
    __asm__ __volatile__("invtlb %0, $zero, $zero" : :
        "i"(0) : "memory");
}

unsigned long loong_scalar_frametrace_csr(void) {
    unsigned long result;
    __asm__ __volatile__("csrrd %0, 0x7" : "=r"(result));
    return result;
}
