/* Linux 6.6.143 arch/arm64/include/asm/tlbflush.h
 * __flush_s2_tlb_range_op() expands ripas2e1is for range invalidation;
 * nVHE mem_protect.c uses vmalls12e1 (non-shareable). */
void tlbi_range_ripas2e1is(unsigned long v) {
    asm volatile(".arch armv8.5-a\n\ttlbi ripas2e1is, %x0" : : "rZ"(v));
}
void tlbi_range_ripas2e1(unsigned long v) {
    asm volatile(".arch armv8.5-a\n\ttlbi ripas2e1, %x0" : : "rZ"(v));
}
void tlbi_range_all_stage12(void) {
    asm volatile(".arch armv8.5-a\n\ttlbi vmalls12e1");
}
