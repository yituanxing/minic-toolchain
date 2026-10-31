/* Linux 6.6.143 asm/kvm_mmu.h exact __kern_hyp_va ALTERNATIVE_CB:
 * 5 original VA-mask instructions and no replacement body, with the
 * explicit kvm_update_va_mask PREL32 patch callback, feature 0x8001.
 * This cannot be replaced by regular .altinstructions two-body logic. */
#define VA_MASK_ALTERNATIVE_CB(oldinstr) \
    ".if 1 == 1\n" \
    "661:\n\t" oldinstr "\n" \
    "662:\n" \
    ".pushsection .altinstructions,\"a\"\n" \
    " .word 661b - .\n" \
    " .word kvm_update_va_mask- .\n" \
    " .hword (1 << 15) | (1)\n" \
    " .byte 662b-661b\n" \
    " .byte 664f-663f\n" \
    ".popsection\n" \
    "663:\n\t" \
    "664:\n\t" \
    ".endif\n"

unsigned long linux_hyp_va(unsigned long value) {
    asm volatile(VA_MASK_ALTERNATIVE_CB(
        "and %0, %0, #1\n"
        "ror %0, %0, #1\n"
        "add %0, %0, #0\n"
        "add %0, %0, #0, lsl 12\n"
        "ror %0, %0, #63\n"
    ) : "+r"(value));
    return value;
}
