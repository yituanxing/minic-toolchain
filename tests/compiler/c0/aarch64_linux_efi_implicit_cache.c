/* Linux 6.6 drivers/firmware/efi/libstub/arm64.c, exact GNU cache-ASM
 * spelling after preprocessing. GNU extended asm with zero outputs is
 * implicitly volatile even when no `volatile` keyword or `memory` clobber
 * appears. This remains a privileged hardware operation. */
void minic_efi_cache_sync_civac(unsigned long image_base,
                                unsigned long code_size,
                                unsigned long lsize) {
    unsigned long base = image_base;
    unsigned long size = code_size;
    do {
        asm("dc " "civac" ", %0" :: "r"(base));
        base += lsize;
        size -= lsize;
    } while (size >= lsize);
    asm("ic ialluis");
    asm volatile("dsb ish" ::: "memory");
    asm volatile("isb" ::: "memory");
}

void minic_efi_cache_sync_cvau(unsigned long base) {
    asm("dc " "cvau" ", %0" :: "r"(base));
}

/* Explicit-volatile without a compiler fence has the same cache effect. */
void minic_efi_cache_sync_explicit(unsigned long base) {
    asm volatile("dc civac, %0" :: "r"(base));
}
