/* The two original Linux APIC AMD alternatives are patchable ELF sites.
 * They must be assembled, never run as userspace instructions. */
#define APIC_AMD_ALTERNATIVE(fallback, replacement, feature) \
    "# ALT: oldnstr\n" \
    "661:\n\t" fallback "\n" \
    "662:\n" \
    "# ALT: padding\n" \
    ".skip -(((6651f-6641f)-(662b-661b)) > 0) * " \
           "((6651f-6641f)-(662b-661b)),0x90\n" \
    "663:\n" \
    ".pushsection .altinstructions,\"a\"\n" \
    " .long 661b - .\n" \
    " .long 6641f - .\n" \
    " .4byte " feature "\n" \
    " .byte 663b-661b\n" \
    " .byte 6651f-6641f\n" \
    ".popsection\n" \
    ".pushsection .altinstr_replacement, \"ax\"\n" \
    "# ALT: replacement 1\n" \
    "6641:\n\t" replacement "\n" \
    "6651:\n" \
    ".popsection\n"

int minic_linux_apic_mmio_amd_alt(int *address, int value) {
    __asm__ volatile(APIC_AMD_ALTERNATIVE("movl %0, %P1",
        "xchgl %0, %P1", "(22*32 + (5))")
        : "=r" (value), "=m" (*address)
        : "i" (0), "0" (value), "m" (*address));
    return value;
}

void minic_linux_apic_divider_amd_alt(void) {
    __asm__ volatile(APIC_AMD_ALTERNATIVE("", "div %2",
        "(22*32 + (1*32+1))")
        : : "a" (0), "d" (0), "r" (1));
}
