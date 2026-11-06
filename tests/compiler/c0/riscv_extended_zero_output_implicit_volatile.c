/* Original Linux RISC-V TLB flush shape: GNU extended asm with zero outputs
 * is implicitly volatile even if it has inputs and a memory clobber. */
void local_flush_tlb_implicit(unsigned long asid) {
    if (asid != ~0UL)
        asm("sfence.vma x0, %0" : : "r"(asid) : "memory");
}

void local_flush_tlb_explicit(unsigned long asid) {
    if (asid != ~0UL)
        asm volatile("sfence.vma x0, %0" : : "r"(asid) : "memory");
}
