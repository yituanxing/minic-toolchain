/* Exact AArch64 Linux cache-maintenance ISA effects, not compiler barriers.
 * GNU is the independent target-assembler oracle for these privileged
 * instructions. They must NOT be executed in QEMU user mode, because the
 * real Linux kernel expects to run them under its privileged cache policy. */

void minic_dc_ivac(unsigned long address) {
    asm volatile("dc ivac, %0" : : "r" (address) : "memory");
}

void minic_dc_cvac(unsigned long address) {
    asm volatile("dc cvac, %0" : : "r" (address) : "memory");
}

void minic_dc_cvau(unsigned long address) {
    asm volatile("dc cvau, %0" : : "r" (address) : "memory");
}

/* This is the actual Linux 6.6.143 alternative-patching operation. */
void minic_dc_civac(unsigned long address) {
    asm volatile("dc civac, %0" : : "r" (address) : "memory");
}

void minic_ic_ivau(unsigned long address) {
    asm volatile("ic ivau, %0" : : "r" (address) : "memory");
}

/* GNU basic asm, lacking the volatile keyword, is implicitly volatile.
 * The original kernel writes its operand separator as a literal TAB. */
void minic_ic_ialluis(void) {
    asm("ic\tialluis");
}

/* The six operations must remain individually representable even when
 * mixed in the same TU and with ordinary scalar/pointer code nearby. */
unsigned long minic_cache_address_arithmetic(unsigned long address,
                                             unsigned long length) {
    unsigned long first = address & ~63UL;
    unsigned long last = (address + length + 63UL) & ~63UL;
    if (last > first) {
        minic_dc_cvau(first);
        minic_dc_cvau(last - 64UL);
    }
    return last;
}

void minic_cache_target_pointer(void *address) {
    asm volatile("dc civac, %0" : : "r" (address) : "memory");
}
